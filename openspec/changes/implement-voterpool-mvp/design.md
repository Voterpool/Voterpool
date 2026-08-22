# Design: Реализация Voterpool (MVP)

## Context

Репозиторий содержит только документацию `docs/00..13` и каркас OpenSpec; кода нет. Реализуется система с нуля по жёстко заданному стеку (docs/00 §1) и структуре проекта (docs/09). Мотивация и объём — в proposal.md; наблюдаемое поведение — в дельтах specs/*. Ключевые ограничения: C++20 с корутинами, Drogon + simdjson + RocksDB + spdlog + jemalloc + yaml-cpp через vcpkg, статический ELF-бинарник, Shared-Nothing (локальный диск), синхронная запись с WAL.

Существенные решения уже зафиксированы документацией (перечислены ниже как данность, а не как предмет выбора): per-proposal mutex registry (docs/01 §5.1), lock-free очередь SSE (docs/04 §1.3.1), Native-токены voterpool_sec_... (docs/03), WriteBatch-атомарность (docs/01 §4), meta:schema_version-миграции (docs/12), Checkpoints (docs/13).

## Goals / Non-Goals

**Goals:**

- Единый статический бинарник `voterpool`, поднимающий POST /mcp, GET /mcp/events, /health, /metrics и CLI `checkpoint`.
- Слоистая архитектура docs/09: core / domain / storage(+repositories) / consensus / server / mcp — с тестируемой бизнес-логикой, изолированной от транспорта.
- Полное покрытие поведения тестами трёх уровней (docs/10): unit → integration → e2e, детерминированное время через IClock/MockClock.
- Воспроизводимая сборка через vcpkg manifest mode; CMake ≥3.20.

**Non-Goals:**

- Enterprise-части docs/00 §6: OIDC/jwt-cpp, rate limiting (-32006 не генерируется в MVP), RocksDB Maintenance Worker, шардирование, облачные бэкапы, Secondary Instance.
- TLS-терминация (server.ssl зарезервирована; в MVP терминирует прокси).
- HTTP/2-обязательность: достаточно HTTP/1.1 Drogon (SSE работает поверх него); включение h2c остаётся конфигурационной опцией без отдельных требований.
- Бенчмарк-таргет NFR-1 как CI-гейт (отдельный `voterpool_bench`, нестабилен в CI — docs/10 §3.3).

## Decisions

### D1. Слои и направление зависимостей

Строго вниз: `mcp` → (`consensus`, `storage`) → `domain`; `server` (middleware, SseHub, workers) оркестрируется из `main.cpp`. Бизнес-логика возвращает `RpcResult<T> = std::expected<T, RpcError>` (C++20-паттерн Result из docs/07 §1.4) и никогда не бросает исключения наружу слоя. Альтернатива (исключения + коды в handler'е) отклонена: теряется тестируемость и растёт риск утечки исключений в циклы Drogon.

### D2. Граница JSON: simdjson на входе, Drogon Json на выходе

Входящие MCP-запросы парсит simdjson On-Demand (NFR-1); исходящие тела и error.data собирает drogon::Json::Value (docs/07 §1.4 примечание). Доменные сущности сериализуются кодеками ТОЛЬКО в слое repositories (единая точка десериализации, docs/12 §3) — бизнес-логика не видит JSON. Альтернатива (simdjson везде) отклонена: On-Demand-документ живёт пока жив буфер, для построения ответов он неудобен, а Drogon-билдер уже в стеке.

### D3. Хранилище: 9 CF, конкатенативные ключи, префиксные итераторы

Схема точно по docs/01: default (агенты + meta:*), cf_organizations, cf_memberships, cf_proposals, cf_votes, cf_indexes (pending:, active_proposals:, proposal_lookup:, org_feed:, tag:, org_name:, join_limit:, category:), cf_auth, cf_agent_orgs, cf_audit_log. Значения — UTF-8 JSON строкой; ключи — `:`-конкатенация; выборки — prefix-scan; лента — created_at_reversed = MAX_TS − created_at; числовые поля времени в ключах — фиксированная ширина (нулевое дополнение) для лексикографического порядка сортировки. Опции RocksDB: WAL on, WriteOptions.sync=true для мутаций (group commit сглаживает fsync между параллельными предложениями — docs/00 NFR-1 примечание), rocksdb::Statistics включена для метрик. Альтернативы: Merge operator для счётчиков — отвергнут (не несёт Early-Exit/voters_count логики, docs/01 §5.1); OptimisticTransactionDB — отвергнут в пользу мьютексов (детерминированность ретраев под 50k RPS).

### D4. Контроль конкурентности: ProposalLock registry

Потокобезопасный реестр proposal_id → std::shared_ptr<std::mutex> с подсчётом ссылок (ленивое создание, удаление после закрытия предложения). cast_vote берёт замок после базовых проверок членства и держит до коммита WriteBatch; TTL closeProposal берёт тот же замок до чтения Proposal (docs/01 §5.2–5.3). Ровно один замок на операцию → тупиков нет. Альтернативы (Merge operator, actor-per-proposal) уже отвергнуты документацией.

### D5. Консенсус: Strategy + фабрика

`IConsensusModel { evaluate(proposal, config); allowed_decisions(); }`; MajorityModel и QuorumModel наследуют общее ядро сравнения (MAJORITY = QUORUM с виртуальным N_eff = N + (T − V) и порогом 50%), ConsentModel отдельно. Фабрика регистрирует модели по строковому идентификатору config.consensus_model. Ранняя математика — строго docs/02 §1.4 (включая Early-Exit Y_max = Y + (T − V)) и §1.5 (голос силой 0.0 меняет только voters_count). Единица применения последствий PASSED одна: общий код applyPassedConsequences(), вызываемый и из cast_vote (досрочное закрытие), и из closeProposal (таймер) — устраняет расхождение двух путей (docs/02 Шаг 5).

### D6. MCP-слой: статическая таблица инструментов

Реестр «имя → обработчик + inputSchema» — constexpr/static таблица в mcp/tools (единый источник истины tools/list и диспетчеризации, docs/05 §1.0). Каталог собирается один раз при старте, сериализуется в кэш ttlMs из конфига; порядок лексикографический. Middleware-пайплайн: pre-handling advice выполняет (0) проверку db_healthy_, (1) мягкую авторизацию (невалидный токен блокируется сразу; отсутствие заголовка проходит анонимно), (2) валидацию MCP-заголовков; McpHandler требует AgentContext для всех инструментов, кроме register_agent/server-discover/tools-list.

### D7. SSE: SseHub + moodycamel::ConcurrentQueue

SseHub держит `unordered_map<org_id, vector<subscriber>>` под shared_mutex; подписка all-orgs вычисляется из cf_agent_orgs при connect. Очередь событий — moodycamel::ConcurrentQueue<SseEvent> (добавить в vcpkg.json рядом с gtest; альтернатива boost::lockfree::queue допускает фиксированную ёмкость хуже). Диспетчер: батчи ≤1000, запись через `queueInLoop` цикла Drogon; проверка alive-состояния стрима перед записью; heartbeat отдельным таймером jthread. Доставка «только ACTIVE организаций» фиксируется на момент подключения; изменения членства применяются при следующем переподключении (принято как допустимое упрощение MVP — соединение короткоживущее относительно жизни членства).

### D8. Воркеры и lifecycle

std::jthread + std::stop_token + condition_variable. TTL-воркер: цикл 1 сек, seek по active_proposals:, break на первом expires_at > now. Startup: RocksDB Open → SchemaGate (миграции до воркеров) → [опционально rebuild индекса] → Drogon + workers → /health 200. Shutdown: quit Drogon → SSE drain (кадр server_shutdown) → request_stop() воркерам → DB Flush+Close → spdlog flush → exit(0). Обработчик сигнала только ставит флаг/queueInLoop(quit) — async-signal-safe.

### D9. Наблюдаемость: собственный мини-реестр вместо библиотеки

core/Metrics.h: Counter/Gauge/Histogram на std::atomic (relaxed), экспозиция текстом при скрейпе. Альтернатива (pull-библиотека prometheus-cpp) отклонена: лишняя зависимость против vcpkg-набора docs/09 при тривиальном формате 0.0.4. Кардинальность — только конечные лейблы (docs/11 §1); RocksDB-статистика — выбранные тикеры под префиксом rocksdb_.

### D10. Тестовая инфраструктура

Точно по docs/10: tests/common (TempDbFixture RAII, MockClock : IClock — время инжектируется в ConsensusEngine и TTL-воркер, TestServer in-process на 127.0.0.1:0, SseClient поверх raw TCP, RandomPort). Регистрация gtest_discover_tests. Concurrency-тесты помечаются для TSan-прогона в CI (CMakePresets). E2E не трогают БД — только публичный контракт.

### D11. Интерпретации пробелов документации (зафиксированные допущения)

1. **category при create_organization**: контракт docs/05 §1.2 НЕ содержит category → создаётся пустой строкой ""; задаётся только через UPDATE_ORG_INFO. Индекс category: строится при первом непустом значении.
2. **Уникальность имени организации**: docs/07 приводит -32003 «Organization with this name already exists» → имя уникально среди ACTIVE-организаций (DISSOLVED не блокируют повторное использование).
3. **list_members**: возвращаются участники со статусом ACTIVE (PENDING скрыты — их список раскрывается только через консенсусный процесс одобрения).
4. **get_organization для DISSOLVED**: читаем любым агентом (публичный профиль), но list_members/join/предложения → -32004.
5. **transfer_admin при SHARES**: сила голоса при передаче не меняется (передаётся роль, не power).
6. **dissolve закрывает активные предложения EXPIRED**: финальная оценка моделью не запускается — статус принудительно EXPIRED, последствия PASSED не применяются; события proposal_closed рассылаются.
7. **join_limit ключи**: чистятся TTL-воркером раз в сутки (compaction filter — Enterprise).
8. **request_timeout_sec не применяется к SSE** (docs/08 комментарий) — соединение держится heartbeat'ами.

## Risks / Trade-offs

- [RocksDB под vcpkg собирается долго и требователен к компилятору C++20-корутин Drogon] → зафиксировать triplet x64-linux-release в vcpkg.json (builtin-baseline), CI-кэш бинарных пакетов; локальная проверка сборки до старта фич.
- [Per-proposal mutex ограничивает масштаб одного «горячего» предложения (сериализация)] → принято документацией сознательно (корректность > пиковый RPS по одному proposal_id); групповой коммит WAL смягчает fsync; NFR-1 формулируется на агрегированной нагрузке.
- [JSON-значения целиком перезаписываются при каждом инкременте счётчиков Proposal (read-modify-write всего документа)] → документы малы (<1 КБ), simdjson/Drogon-парсинг микросекундный; альтернативные блоб-форматы усложнили бы миграции docs/12 без измеримой выгоды.
- [Гонка SSE-стримов при закрытии соединения во время записи диспетчера] → единый владелец записи (цикл Drogon через queueInLoop), alive-check перед отправкой, счётчик sse_write_failures для мониторинга.
- [Миграции при живых индексах могут оставить рассинхрон при ошибке] → правило docs/12 §5: переиндексация тем же WriteBatch; идемпотентные шаги; integration-тест test_migration с crash-сценарием.
- [Деградированный режим может «залечь» незамеченным] → voterpool_db_healthy=0 — page-уровень алерта (docs/11 §5); fail-fast /health 503 убивает pod оркестратором.
- [Объём MVP велик — риск расползания scope] → задачи tasks.md срезаны по MVP-перечню docs/00 §6; любые Enterprise-идеи фиксируются в Non-Goals, а не в задачах.

## Migration Plan

Новая система — миграции данных отсутствуют; развёртывание = первый запуск бинарника (инициализация meta:schema_version). Механизм миграций (SchemaVersion runner + шаг MigrateV{n}_To_V{n+1}) реализуется сразу с одним примером-холостым шагом и gate-тестами, чтобы будущие версии схемы эволюционировали по docs/12 без переписывания ядра. Откат версии — рестарт предыдущим бинарником невозможен после повышения схемы (gate exit(1)); эксплуатационный откат — восстановление из checkpoint (docs/13 §5).

## Open Questions

Нет вопросов, блокирующих спецификации или декомпозицию: все спорные места закрыты решениями D11 как зафиксированными допущениями; они пересматриваются только вместе со спецификациями.
