OpenSpec: Стратегия тестирования (GoogleTest / Integration / E2E)
Версия: 1.0.0
Статус: Draft

# 1. Цели и принципы

Весь функциональный объём системы покрывается автоматическими тестами трёх уровней (классическая пирамида). Тесты живут в ОТДЕЛЬНОЙ папке `tests/`, собираются только при `VOTERPOOL_BUILD_TESTS=ON` и запускаются через `ctest`.

Принципы:

Независимость: каждый тест получает СОБСТВЕННУЮ временную директорию RocksDB (RAII-фикстура); глобального состояния нет — набор безопасно запускается параллельно.
Детерминированность: время инжектируется через интерфейс `IClock`; в unit/integration тестах используется `MockClock` с ручным продвижением. Никаких `sleep`-based утверждений; асинхронные ожидания — poll с таймаутом (до 5 сек).
Изоляция сети: E2E поднимает сервер ТОЛЬКО на `127.0.0.1` со случайным свободным портом; внешняя сеть не используется.
Скорость: unit — миллисекунды; integration — секунды; e2e — десятки секунд на весь набор.
Единый фреймворк: GoogleTest (gtest) для всех уровней.

# 2. Уровни тестов

## 2.1. Unit-тесты (tests/unit/)

Чистая логика без I/O. Быстрые, исчерпывающие по краевым случаям.

Консенсус (математика docs/02):
- MAJORITY: Y > T/2 → PASSED; N ≥ T/2 → ранний REJECTED; статус EXPIRED НЕВОЗМОЖЕН ни при каком исходе таймера; неявка трактуется как ПРОТИВ; allowed_decisions = {YES, NO}.
- QUORUM_PERCENTAGE: явка V = Y + N (ABSTAIN отсутствует и отвергается); PASSED при V ≥ Qreq И Y > N; EXPIRED при недоборе кворума к таймеру; Early-Exit — корректный пример из docs/02 §1.5 (T=100, Qreq=80, N=60 → немедленный REJECTED) И контрпример (N=30 при тех же условиях — PASSED ещё возможен, закрывать НЕЛЬЗЯ); замороженный T: вступление участника после создания предложения НЕ меняет порогов.
- CONSENT: N == 0 И voters_count > 0 → PASSED; любой NO → REJECTED; все воздержались (N==0, Y==0, voters_count>0) к таймеру → EXPIRED; allowed_decisions = {YES, NO, ABSTAIN}; голос силой 0.0 увеличивает voters_count, но не силы.
- Фабрика моделей: регистрация по строковому идентификатору; неизвестная модель → ошибка.

Ошибки JSON-RPC (docs/07): маппинг RpcError → тело ответа; поле data обязательно для кастомных кодов; Parse error (-32700) → HTTP 400, остальные ошибки → HTTP 200.
Валидация параметров: UUID-формат; decision вне allowed_decisions() → -32005; одновременные config_delta и action → -32005; неизвестный action.kind → -32602.
Ключи хранилища: построители всех ключей CF и вторичных индексов (включая created_at_reversed для ленты, нижний регистр тегов/категорий).

## 2.2. Integration-тесты (tests/integration/)

РЕАЛЬНЫЙ RocksDB во временной директории + реальные репозитории, ConsensusEngine, воркеры. Проверяют то, что нельзя проверить моками: транзакционность, конкурентность, восстановление.

Инфраструктура: фикстура `TempDbFixture` — RAII: уникальная директория `<temp>/voterpool_test_<pid>_<n>` создаётся в SetUp, удаляется `remove_all` в TearDown.

Покрываемые сценарии:
- Схема: открытие БД создаёт ВСЕ Column Families (default, cf_organizations, cf_memberships, cf_proposals, cf_votes, cf_indexes, cf_auth, cf_agent_orgs); round-trip каждой сущности (JSON serialize/deserialize без потерь полей, включая created_at/updated_at).
- Транзакционность cast_vote: имитация сбоя между шагами не оставляет частичного состояния (голос без счётчиков или наоборот); после любого исхода сумма счётчиков == сумме power_at_vote записанных голосов.
- Конкурентность: N потоков одновременно голосуют по ОДНОМУ предложению → ноль потерянных обновлений (финальные yes/no/abstain_power точны), ровно один голос на агента (повторный → -32003), ровно одно закрытие предложения (гонка cast_vote vs TTL closeProposal под общим ProposalLock).
- ACTION-предложения: APPROVE_MEMBER активирует PENDING-участника (total_voting_power инкрементально, cf_agent_orgs создан, pending: удалён, join_limit: инкрементирован); повторная активация — идемпотентна; превышение max_agents/joins_per_day_limit блокирует применение. UPDATE_ORG_INFO обновляет метаданные и АТОМАРНО переиндексирует org_name:/tag:/category: (старые ключи удалены, новые созданы, в одном WriteBatch).
- Discovery-индексы: лента org_feed:ACTIVE — новые первыми (reversed timestamp); пересечение тегов (AND-merge сканов); префиксный поиск по имени; фильтр по category; DISSOLVED исключены из всех выдач; дневной лимит вступлений.
- TTL-воркер: MockClock.advance() → воркер закрывает просроченное под тем же ProposalLock; итоговый статус соответствует модели (PASSED/REJECTED/EXPIRED); повторная обработка исключена (ключ индекса удаляется первым).
- Recovery: записать данные → закрыть БД → переоткрыть → полное состояние консистентно (WAL replay); опциональный rebuild индекса active_proposals восстанавливается сканом cf_proposals.
- Auth Native: register_agent генерирует voterpool_sec_... токен; в cf_auth лежит только SHA-256 хэш; валидный токен → AgentContext; невалидный → -32001.
- Graceful stop воркеров: request_stop() завершает циклы; очередь SSE дренируется.
- Миграции схемы: фикстура «БД версии N» → старт движка → версия N+1, дефолты проставлены, данные сохранены; обрыв посреди миграции дозавершается (идемпотентность); gate «данные новее бинарника» → exit(1) (docs/12 §6).
- Деградированный режим: установка db_healthy_ = false → ВСЕ новые MCP мгновенно -32050/HTTP 503, GET /health → 503, checkpoint отказывается работать (docs/07 §1.5).
- Checkpoint/restore: CreateCheckpoint во временную директорию → развернуть в новую storage.path → БД открывается, все данные читаемы (docs/13 §5).

## 2.3. E2E-тесты на C++ (tests/e2e/)

Поднимается НАСТОЯЩИЙ сервер (Drogon in-process) на `127.0.0.1:<случайный свободный порт>` с временной директорией RocksDB; тесты общаются с ним реальным HTTP-клиентом как внешние агенты — только через публичный контракт MCP/SSE, без доступа к БД.

Инфраструктура: `TestServer` — конфиг с port=0 (или выбранным свободным портом), temp db dir, запуск подсистем как в main(); готовность — poll `GET /health` → 200. `SseClient` — минимальный читатель SSE-кадров поверх HttpClient/raw-socket с таймаутами.

Покрываемые сценарии (полный жизненный цикл через HTTP):
- Happy path агентов: register_agent ×N → create_organization (OPEN и CLOSED) → join (OPEN мгновенно ACTIVE; CLOSED → PENDING) → консенсусное одобрение (ACTION APPROVE_MEMBER голосованием) → create_proposal → cast_vote всеми участниками → PASSED → config_delta/config применились → get_proposals(COMPLETED) содержит агрегаты.
- Governance: UPDATE_ORG_INFO проходит → get_organization отражает новые метаданные и category; leave_organization (последний админ — -32005); transfer_admin; dissolve_organization → организация пропала из search_organizations, get_organization возвращает status=DISSOLVED, операции → -32004.
- Лимиты: заполнение max_agents; исчерпание joins_per_day_limit за сутки.
- Ошибки поверх HTTP: весь каталог -32001..-32005 и стандартные -32601/-32602/-32600; невалидный JSON → HTTP 400 + -32700.
- Изоляция: агент организации A не видит предложения организации B; чужой org_id в cast_vote разрешается через lookup, но членство отсекает (-32002); SSE чужой организации не доставляет события.
- SSE: подписка до действий; получение proposal_created, vote_cast, proposal_closed, member_joined/member_left, admin_transferred, organization_dissolved; heartbeat `: keep-alive` каждые ~15 сек (тест с сокращённым интервалом конфигурации).
- Завершение: SIGTERM процессу → drain SSE (кадр server_shutdown) → процесс завершается с кодом 0; /health перестаёт отвечать.
- Протокол MCP 2026-07-28: отсутствие обязательных заголовков (MCP-Protocol-Version, Mcp-Method, Mcp-Name) → -32600; несоответствие Mcp-Name = params.name → -32600; server/discover возвращает protocolVersion/extensions; tools/list содержит ttlMs/cacheScope и детерминированный порядок; tools/call и прямой method=<tool> идентичны по результату; неизвестное имя → -32601.
- Метрики: скрейп GET /metrics возвращает валидный Prometheus-формат; voterpool_mcp_requests_total растёт после вызовов.

# 3. Инфраструктура тестирования

## 3.1. Зависимости и сборка

- vcpkg.json: зависимость `gtest` добавлена в манифест (см. docs/09).
- CMake: при `VOTERPOOL_BUILD_TESTS=ON` — `find_package(GTest CONFIG REQUIRED)`, `add_subdirectory(tests)`, регистрация через `gtest_discover_tests` (каждый тест — отдельный ctest-кейс).
- Тестовые таргеты НЕ линкуют jemalloc-переопределения принудительно там, где это мешает санитайзерам; ASan/TSan-прогоны — опциональные пресеты CMakePresets (concurrency-тесты обязательно прогоняются под TSan в CI).

## 3.2. Общие помощники (tests/common/)

TempDbFixture.h — RAII-фикстура временной директории RocksDB (уникальной на тест).
MockClock.h — реализация IClock: now() возвращает управляемое значение; advance(seconds).
TestServer.h — запуск движка in-process на случайном порту с temp db; ожидание /health.
SseClient.h — подключение и разбор SSE-кадров с таймаутами.
RandomPort.h — получение свободного порта (bind :0).

## 3.3. Правила качества

Один логический сюжет на тест; имя TEST(Suite, Condition_Expectation).
Общие приготовления — через TEST_F и фикстуры, без копипасты.
Никаких зависимостей от окружения (пути, время суток, сеть); параллельный запуск ctest безопасен.
Каждый MCP Tool: минимум happy-path + типовые ошибки. Каждая модель консенсуса: полная таблица исходов. Каждый код ошибки: минимум один тест. Каждый вторичный индекс: порядок + фильтрация.
Производительность (NFR-1) — ОТДЕЛЬНЫЙ бенчмарк-таргет `voterpool_bench`; в дефолтный ctest-набор не входит (нестабилен в CI).

# 4. Карта покрытия (требование → уровень → файл)

| Требование | Уровень | Файл |
|---|---|---|
| Математика консенсуса, frozen T, Early-Exit | unit | test_consensus_*.cpp |
| Per-model варианты голоса | unit | test_consensus_models.cpp |
| Ошибки/валидация JSON-RPC | unit | test_jsonrpc_errors.cpp |
| Ключи и индексные форматы | unit | test_keys.cpp |
| Схема CF, round-trip сущностей, таймстампы | integration | test_storage_repos.cpp |
| Атомарность cast_vote, конкурентность, ProposalLock | integration | test_concurrency.cpp |
| ACTION: одобрение/правка орги, переиндексация | integration | test_actions.cpp |
| Discovery: лента, теги, category, лимиты | integration | test_discovery_index.cpp |
| TTL-воркер, таймеры | integration | test_ttl_worker.cpp |
| Recovery/WAL после рестарта | integration | test_recovery.cpp |
| Auth Native (токены, cf_auth) | integration | test_auth_native.cpp |
| Миграции схемы meta:schema_version | integration | test_migration.cpp |
| Checkpoint/restore бэкапов | integration | test_checkpoint_restore.cpp |
| Деградированный режим (-32050 / HTTP 503) | integration | test_degraded_mode.cpp |
| MCP-протокол 2026-07-28 (заголовки, discover, tools/list, tools/call) | e2e | test_protocol.cpp |
| Полный жизненный цикл через MCP | e2e | test_mcp_flow.cpp |
| SSE-события, heartbeat, изоляция стримов | e2e | test_sse_events.cpp |
| Каталог ошибок поверх HTTP | e2e | test_errors_http.cpp |
| Graceful shutdown | e2e | test_shutdown.cpp |
| Пропускная способность (NFR-1) | bench | bench/voterpool_bench.cpp |
