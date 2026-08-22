# Tasks: Реализация Voterpool (MVP)

## 1. Каркас проекта и сборка

- [ ] 1.1 Создать структуру директорий по docs/09 (include/{core,domain,storage/repositories,consensus,server,mcp/tools}, src/, tests/{common,unit,integration,e2e}, config/) и корневой CMakeLists.txt (C++20, GLOB_RECURSE src, опция VOTERPOOL_BUILD_TESTS, -static-libgcc/-static-libstdc++, JEMALLOC_NO_DEMANGLE); проверить конфигурацию CMake без ошибок
- [ ] 1.2 Создать vcpkg.json с зависимостями drogon, rocksdb, simdjson, spdlog, jemalloc, yaml-cpp, gtest, moodycamel::concurrentqueue (jwt-cpp исключён — design D11) и выполнить vcpkg install; убедиться, что все find_package находятся
- [ ] 1.3 Собрать пустой исполняемый целевой voterpool (заглушка main.cpp) и проверить запуск бинарника; зафиксировать в README-заглушке команды сборки

## 2. Ядро (core)

- [ ] 2.1 Реализовать core/Config.h/.cpp: структуры секций server/storage/auth/sse/metrics/mcp/logging/rate_limit по docs/08 §1.2, загрузка yaml-cpp, значения по умолчанию; юнит-тест парсинга полного config/default.yaml проверяет каждое поле
- [ ] 2.2 Реализовать env-переопределения VOTERPOOL_{SECTION}_{KEY} и CLI-флаги --config/--port/--db-path/--log-level/--daemon с приоритетом CLI > env > файл; юнит-тесты приоритетов и daemon-fork проходят
- [ ] 2.3 Реализовать валидацию конфига (отрицательный порт, voting_duration_sec <= 0 и т.п.) → stderr + exit(1), и проверку access(storage.path, R_OK|W_OK) → critical + exit(1); юнит-тесты невалидных конфигов завершаются ожидаемым кодом
- [ ] 2.4 Реализовать core/Logger.h: инициализация spdlog async (очередь 8192, формат из конфига, stdout или log_file), уровни; тест проверяет запись и flush при остановке
- [ ] 2.5 Реализовать core/Metrics.h: реестр Counter/Gauge/Histogram на std::atomic (relaxed) с сериализацией в текстовый формат Prometheus 0.0.4 (# HELP/# TYPE) при скрейпе; юнит-тест формата вывода и инкрементов проходит
- [ ] 2.6 Реализовать core/IClock.h + SystemClock/MockClock (advance(seconds)) для инжекции времени; подключить jemalloc-инициализацию (core/Memory.h); тест MockClock продвигает время детерминированно

## 3. Домен

- [ ] 3.1 Реализовать POCO-сущности domain/: Agent, Organization (+Config), Membership, Proposal (+Action/config_delta), Vote, AuditEvent, SseEvent с enum'ами (типы, статусы, роли, модели консенсуса, решения YES/NO/ABSTAIN); компиляция без предупреждений -Wall -Wextra
- [ ] 3.2 Реализовать кодеки JSON (serialize/deserialize) для каждой сущности в слое storage/repositories (единая точка схемы — design D2, docs/12 §3): round-trip тесты без потери полей, включая created_at/updated_at и сохранение неизвестных полей при parse-mutate-serialize

## 4. Хранилище

- [ ] 4.1 Реализовать storage/RocksDBWrapper.h/.cpp: открытие 9 CF (default, cf_organizations, cf_memberships, cf_proposals, cf_votes, cf_indexes, cf_auth, cf_agent_orgs, cf_audit_log), опции WAL/write_buffer из конфига, WriteOptions.sync=true для мутаций, Statistics включена, Flush+Close; integration-тест открытия всех CF во временной директории
- [ ] 4.2 Реализовать построители ключей всех CF и вторичных индексов (agent:, org:, org:{}:member:{}, org:{}:proposal:{}, ...:vote:{}, auth:{hash}, agent_orgs:{}, audit:{}:{ms}:{seq}, pending:, active_proposals:, proposal_lookup:, org_feed:{status}:{ts_reversed}:, tag:, org_name:, join_limit:{org}:{yyyymmdd}, category:) с фиксированной шириной временных полей и нижним регистром тегов/имён/категорий; unit test_keys.cpp покрывает каждый формат
- [ ] 4.3 Реализовать репозитории AgentRepository/OrgRepository/ProposalRepository/VoteRepository (+MembershipRepository, IndexRepository, AuditLogRepository): get/put/delete через кодеки, prefix-scan выборки, WriteBatch-помощник; integration test_storage_repos.cpp — round-trip всех сущностей
- [ ] 4.4 Реализовать аудит-запись тем же WriteBatch, что и мутация (ключ audit:{org_id}:{created_at_ms}:{seq}), чтение истории обратным итератором; integration-тест: мутация силы оставляет ровно одну аудито-запись, ключи не удаляются после роспуска

## 5. Авторизация и изоляция

- [ ] 5.1 Реализовать IAuthProvider + NativeAuthProvider: генерация криптостойкого токена voterpool_sec_..., хранение только SHA-256-хэша в cf_auth, O(1)-валидация → AgentContext{agent_id, auth_provider}; integration test_auth_native.cpp: валидный/невалидный токен, в БД нет открытых ключей
- [ ] 5.2 Реализовать проверки доступа бизнес-уровня: DISSOLVED → -32004, отсутствие членства/PENDING → -32002, ADMIN-only операции → -32002, кросс-тенантные обращения отсечены; unit/integration-тесты матрицы прав проходят

## 6. Консенсус

- [ ] 6.1 Реализовать IConsensusModel + фабрику по строковому идентификатору (MAJORITY/QUORUM_PERCENTAGE/CONSENT), allowed_decisions per-model ({YES,NO} ×2, {YES,NO,ABSTAIN}); unit-тест фабрики и наборов вариантов
- [ ] 6.2 Реализовать MajorityModel (PASSED Y > T/2, ранний REJECTED N ≥ T/2, EXPIRED невозможен) на общем ядре сравнения с QuorumModel (N_eff = N + (T − V)); полная таблица исходов в test_consensus_majority.cpp
- [ ] 6.3 Реализовать QuorumModel (PASSED V ≥ Qreq ∧ Y > N; REJECTED V ≥ Qreq ∧ N ≥ Y; EXPIRED таймер ∧ V < Qreq); unit-тесты включая замороженную T
- [ ] 6.4 Реализовать ConsentModel (PASSED N==0 ∧ voters>0; REJECTED N>0; EXPIRED voters==0 или все воздержались), учёт голоса силой 0.0 (только voters_count); unit-тесты полной таблицы исходов
- [ ] 6.5 Реализовать Early-Exit: Y_max = Y + (T − V) против порога модели, недостижимый кворум T < Qreq; unit-тесты корректного примера (T=100, Qreq=80, N=60 → REJECTED) и контрпримера (N=30 → ACTIVE)
- [ ] 6.6 Реализовать consensus/ProposalLock.h: реестр proposal_id → mutex с подсчётом ссылок и очисткой после закрытия; integration test_concurrency.cpp: N потоков голосуют — ноль потерянных обновлений, один голос на агента, гонка cast_vote/closeProposal даёт ровно одно закрытие
- [ ] 6.7 Реализовать ConsensusEngine::cast_vote: атомарный WriteBatch (проверки → запись Vote → счётчики → evaluate → возможное закрытие → последствия PASSED → аудит) под proposal-lock; integration test_cast_vote_tx.cpp: сбой между шагами не оставляет частичного состояния, инвариант суммы счётчиков

## 7. Предложения и ACTION

- [ ] 7.1 Реализовать create_proposal: валидация ACTIVE-членства, expires_at = created_at + voting_duration_sec, инициализация счётчиков, фиксация total_voting_power_at_creation, ровно одно из config_delta/action (-32005), валидация action.kind/payload (-32602), запись proposal_lookup и active_proposals индексов, событие proposal_created; integration/e2e-тесты happy path и ошибок
- [ ] 7.2 Реализовать единый applyPassedConsequences(): config_delta применяется к организации только для будущих предложений; APPROVE_MEMBER активирует PENDING (total_voting_power инкрементально, cf_agent_orgs, удаление pending:, инкремент join_limit:, идемпотентность, проверка лимитов при активации — закрытие без применения действия); UPDATE_ORG_INFO применяет дельту с атомарной переиндексацией org_name:/tag:/category:; вызывается и из cast_vote, и из closeProposal; integration test_actions.cpp покрывает оба действия, идемпотентность и лимиты
- [ ] 7.3 Реализовать get_proposals с фильтром ACTIVE/COMPLETED/ALL и агрегатами (включая total_voting_power_at_creation); e2e-тест фильтрации

## 8. Организации и членство

- [ ] 8.1 Реализовать create_organization/join_organization: создание с конфигом (валидация voting_duration_sec > 0 → -32005), создатель = единственный ADMIN (power 100.0 SHARES / 1.0 EQUAL), уникальность имени среди ACTIVE → -32003, OPEN → мгновенный ACTIVE MEMBER с проверкой max_agents/joins_per_day_limit → -32005, CLOSED → PENDING идемпотентно, DISSOLVED → -32004, member_joined событие; integration/e2e-тесты всех веток
- [ ] 8.2 Реализовать update_voting_power (ADMIN-only, SHARES-only, инкрементальный пересчёт total, сумма > 100% → -32005, аудит POWER_CHANGED); unit+integration-тесты
- [ ] 8.3 Реализовать leave_organization (последний админ → -32005; декремент total; cf_agent_orgs удаление; member_left), transfer_admin (ADMIN-only, цель ACTIVE MEMBER, ровно один админ, аудит + admin_transferred), dissolve_organization (ADMIN-only; статус DISSOLVED; атомарная чистка org_feed/tag/org_name/category индексов; активные предложения → EXPIRED; аудит ORG_DISSOLVED; organization_dissolved; последующие операции -32004, профиль читаем); integration/e2e-тесты
- [ ] 8.4 Реализовать list_members/get_organization/get_agent/update_agent по контрактам docs/05 §1.8–1.12, §1.16 (частичные апдейты профиля, членства ACTIVE и PENDING в get_agent, DISSOLVED профиль читаем); e2e-тесты контрактов ответов

## 9. Discovery

- [ ] 9.1 Реализовать search_organizations: merge-scan ленты org_feed:ACTIVE (новые первыми через created_at_reversed), префикс/подстрока org_name:, AND-пересечение tag:-сканов, фильтр category:/type, limit 1..100 (default 50) и курсорная пагинация без пересечений страниц; integration test_discovery_index.cpp: порядок ленты, AND-теги, регистронезависимость, исключение DISSOLVED, пагинация

## 10. MCP-слой и протокол

- [ ] 10.1 Реализовать mcp/JsonRpcError.h: RpcError{code,message,data} + сборка JSON-RPC error/response (data обязателен для -32xxx), HTTP-маппинг (бизнес-ошибки → 200, -32700 → 400, -32050 → 503); unit test_jsonrpc_errors.cpp каталога кодов
- [ ] 10.2 Реализовать McpHandler: разбор simdjson On-Demand, проверка заголовков MCP-Protocol-Version/Mcp-Method/Mcp-Name (-32600), режим A tools/call (совпадение Mcp-Name = params.name) и deprecated режим B, эквивалентность результатов, -32601 для неизвестных имён, оборачивание результата в content[0].text, _meta.clientInfo → логи/метрики; unit test_params_validation.cpp + каркас e2e test_protocol.cpp
- [ ] 10.3 Реализовать статическую таблицу инструментов «имя → обработчик + inputSchema» (16 инструментов MVP), генерацию tools/list (лексикографический порядок, ttlMs/cacheScope из конфига, draft 2020-12 схемы) и анонимный server/discover (protocolVersion, capabilities.tools, extensions io.voterpool/domain-events → /mcp/events, serverInfo); e2e-тесты discover/tools_list/режимов A/B
- [ ] 10.4 Подключить все инструменты к таблице: register_agent (анонимный), create_organization, join_organization, create_proposal, get_proposals, cast_vote (разрешение org_id через proposal_lookup за O(1)), list_members, update_voting_power, search_organizations, get_organization, get_agent, leave_organization, transfer_admin, dissolve_organization, update_agent; e2e test_mcp_flow.cpp полного жизненного цикла агентов (docs/10 §2.3)
- [ ] 10.5 Реализовать AuthMiddleware (pre-handling): проверка db_healthy_ первой строкой (-32050/503), мягкий режим Authorization (невалидный токен блокируется -32001, отсутствие заголовка — анонимно), внедрение AgentContext в атрибуты запроса; исключения /health, /metrics; e2e test_errors_http.cpp всего каталога ошибок поверх HTTP

## 11. SSE

- [ ] 11.1 Реализовать GET /mcp/events: Bearer-авторизация, вычисление all-orgs списка ACTIVE-членств из cf_agent_orgs при подключении, заголовки text/event-stream/no-cache/keep-alive/X-Accel-Buffering:no, удержание стрима Drogon; e2e-тест установки подписки и отклонения без токена
- [ ] 11.2 Реализовать SseHub (shared_mutex-реестр org_id → подписчики, add/remove при connect/onClose, alive-check) + moodycamel::ConcurrentQueue<SseEvent> и воркер-диспетчер (батчи ≤1000, доставка через queueInLoop, drain при остановке, sse_write_failures для мёртвых стримов); integration-тест конкурентной очереди
- [ ] 11.3 Реализовать генерацию 7 событий с payload по docs/06 §1.3 из точек мутаций (create_proposal, cast_vote, закрытия, join/approve, leave, transfer_admin, dissolve) в формате event:/data:\n\n и FIFO-порядок внутри организации; heartbeat : keep-alive каждые sse.heartbeat_interval_sec (jthread-таймер); e2e test_sse_events.cpp: кадры, payloads, изоляция чужих организаций, heartbeat с сокращённым интервалом

## 12. Воркеры и жизненный цикл

- [ ] 12.1 Реализовать Proposal Expiration Worker: цикл 1 сек, seek/prefix-scan active_proposals: с остановкой на первом неистёкшем, closeProposal под тем же proposal-lock (удаление ключа индекса первым делом в транзакции), финальная оценка моделью, применение последствий PASSED, proposal_closed, метрики ttl_scans/ttl_scan_duration; integration test_ttl_worker.cpp с MockClock.advance
- [ ] 12.2 Реализовать graceful shutdown SIGTERM/SIGINT: async-signal-safe установка флага → stop Drogon → SSE drain с кадром server_shutdown → request_stop() jthread'ов (TTL завершает итерацию, диспетчер дренирует очередь) → DB Flush+Close → spdlog flush → exit(0); e2e test_shutdown.cpp: код 0, кадр server_shutdown, данные читаемы после рестарта
- [ ] 12.3 Реализовать startup-recovery: Open с WAL-replay → SchemaGate до воркеров → опциональный rebuild индекса active_proposals сканом cf_proposals → старт воркеров/Drogon → /health 200; integration test_recovery.cpp: kill-9 посреди нагрузки → рестарт → консистентное состояние

## 13. Наблюдаемость и деградация

- [ ] 13.1 Реализовать маршруты GET /health (анонимный, 200/503) и GET /metrics (анонимный, text/plain version=0.0.4, полный каталог метрик docs/11 §3 + выборочные rocksdb_* тикеры, конфиг metrics.enabled/path, доступность при деградации); e2e-тест скрейпа и роста voterpool_mcp_requests_total/voterpool_votes_cast_total
- [ ] 13.2 Реализовать degraded mode: IOError/Corruption от Write/Open/Flush → db_healthy_=false + critical лог + db_write_failures_total; отклонение новых POST /mcp мгновенным -32050/HTTP 503 до разбора тела; штатное завершение in-flight запросов; отсутствие автовосстановления до рестарта; integration test_degraded_mode.cpp всех пунктов
- [ ] 13.3 Реализовать структурное логирование каждого запроса (request_id, agent_id, tool/method, outcome ok/error, длительность, clientInfo) без api_key в логах; e2e-тест наличия полей записей

## 14. Схема, миграции и бэкапы

- [ ] 14.1 Реализовать storage/SchemaVersion.h: VOTERPOOL_SCHEMA_VERSION, инициализация meta:schema_version на пустой БД, чтение при старте, цепочка миграций до воркеров, gate «данные новее бинарника» → critical + exit(1); runner батчей по ~10k с Flush и метрикой voterpool_schema_migration_records_total{from,to}
- [ ] 14.2 Написать каркас шага MigrateV{n}_To_V{n+1} (идемпотентный, parse-mutate-serialize с сохранением неизвестных полей, переиндексация затронутых ключей тем же батчем) на примере добавления поля-заготовки; integration test_migration.cpp: апгрейд с дефолтами, crash-тест дозавершения, gate-тест exit(1)
- [ ] 14.3 Реализовать CLI-режим `voterpool checkpoint --config <path> --path <dir>`: offline-открытие (без воркеров/HTTP), CreateCheckpoint, guard от больной базы (не открылась/corruption → отказ exit(1)), ошибка LOCK при живом движке, exit(0)/stderr при успехе/ошибке; integration test_checkpoint_restore.cpp: снимок → развёртывание в новую директорию → открытие со всеми данными

## 15. E2E-контур и финальная верификация

- [ ] 15.1 Собрать tests/common: TempDbFixture (RAII, уникальная директория на тест), TestServer (in-process, 127.0.0.1:случайный порт, poll /health → 200), SseClient (парсер кадров с таймаутами), RandomPort; CMake tests/ с gtest_discover_tests, пресеты CMakePresets (asan/tsan), concurrency-тесты под TSan прогоняются локально
- [ ] 15.2 Написать e2e test_protocol.cpp (заголовки MCP, discover, tools/list ttlMs/cacheScope/порядок, режимы A/B, -32600/-32601) и test_mcp_flow.cpp (register×N → OPEN/CLOSED организации → консенсусное одобрение → предложение → голоса → PASSED → config_delta применился → COMPLETED агрегаты; governance: UPDATE_ORG_INFO, leave/transfer/dissolve; лимиты; изоляция тенантов)
- [ ] 15.3 Прогнать полный ctest (unit + integration + e2e) зелёным и проверить соответствие карты покрытия docs/10 §4; собрать релизный бинарник и убедиться в отсутствии обязательных .so-зависимостей вне glibc (ldd), ручной smoke-тест: старт с default.yaml → register_agent → create_organization → proposal → vote → PASSED → события SSE → checkpoint → рестарт с сохранением состояния
