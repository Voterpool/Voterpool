# Proposal: Реализация приложения Voterpool (MVP)

## Why

Документация в `docs/00..13` полностью описывает автономный Headless-движок коллективных решений гетерогенных ИИ-агентов (Voterpool): доменную модель, математику консенсуса, MCP-контракты, схему хранения и эксплуатационные требования. Кода проекта ещё нет (репозиторий содержит только документацию и каркас OpenSpec). Нужно реализовать приложение по этой документации, получив работающий нативный бинарник с полным MVP-функционалом.

## What Changes

- Создаётся C++20 проект Voterpool с нуля: CMake (≥3.20) + vcpkg manifest mode; стек строго по docs/00 §1 — Drogon (HTTP/SSE-транспорт), simdjson (парсинг входящих запросов), RocksDB (embedded-хранилище, WAL, синхронная запись), spdlog (async-логирование), jemalloc, yaml-cpp; результат сборки — статически слинкованный ELF-бинарник (NFR-4).
- Реализуется протокольное ядро MCP 2026-07-28 (stateless, без хендшейка и сессий): POST /mcp, обязательные заголовки, режим A tools/call + deprecated режим B (method=<tool>), server/discover, кэшируемый tools/list (ttlMs/cacheScope, детерминированный порядок), единый контракт ошибок JSON-RPC (docs/07).
- Реализуются все MVP MCP Tools (16 инструментов): register_agent, create_organization, join_organization, create_proposal (включая ACTION: APPROVE_MEMBER | UPDATE_ORG_INFO и config_delta), get_proposals, cast_vote, list_members, update_voting_power, update_agent, search_organizations, get_organization, get_agent, leave_organization, transfer_admin, dissolve_organization.
- Реализуются все 3 модели консенсуса (MAJORITY, QUORUM_PERCENTAGE, CONSENT) по точной математике docs/02: замороженная T, Early-Exit оптимизация, наборы вариантов голоса per-model, распределение сил EQUAL/SHARES.
- Реализуется слой персистентности RocksDB по docs/01: 9 Column Families, конкатенативные ключи, вторичные индексы (pending, active_proposals, proposal_lookup, org_feed, tag, org_name, join_limit, category), атомарность через WriteBatch, контроль конкурентности через per-proposal mutex registry, аудит cf_audit_log.
- Реализуется версионирование схемы БД и автоматические миграции (meta:schema_version, идемпотентные шаги, gate «данные новее бинарника» → exit(1)) и бэкапы через RocksDB Checkpoints (CLI `./voterpool checkpoint`).
- Реализуется Native-авторизация (токены voterpool_sec_..., SHA-256 хэш в cf_auth, интерфейс IAuthProvider с местом под Enterprise OIDC) и изоляция тенантов организаций.
- Реализуется SSE-расширение io.voterpool/domain-events: GET /mcp/events (all-orgs подписка), каталог событий (proposal_created, vote_cast, proposal_closed, member_joined, member_left, admin_transferred, organization_dissolved), SseHub, heartbeat каждые 15 сек.
- Реализуются фоновые воркеры: Proposal Expiration Worker (TTL, prefix-scan индекса, closeProposal под общим per-proposal мьютексом), SSE Event Dispatcher (lock-free очередь, батчи), грациозный shutdown (SIGTERM/SIGINT) и startup-recovery.
- Реализуется наблюдаемость: GET /metrics (каталог метрик Prometheus docs/11, анонимно), GET /health, деградированный режим БД (-32050 / HTTP 503, backpressure).
- Реализуется конфигурация: config.yaml (yaml-cpp), переопределение переменными окружения VOTERPOOL_{SECTION}_{KEY}, CLI-флаги (--config, --port, --db-path, --log-level, --daemon), валидация прав на storage.path.
- Создаётся трёхуровневый тестовый контур (GoogleTest): unit / integration (реальный RocksDB во временных директориях) / e2e (реальный сервер на 127.0.0.1:случайный порт) по docs/10, включая карту покрытия.

Вне области (Enterprise, отложено по docs/00 §6): OIDC-авторизация, rate limiting, RocksDB Maintenance Worker, инфраструктурное шардирование.

## Capabilities

### New Capabilities

- `mcp-protocol`: Транспортное и протокольное ядро MCP 2026-07-28 — POST /mcp, заголовки, диспетчеризация tools/call / прямой метод, server/discover, tools/list с ttlMs/cacheScope, контракт ошибок JSON-RPC и их HTTP-маппинг (docs/05, docs/07).
- `agent-identity`: Регистрация агентов, Native-токены voterpool_sec_..., хэширование SHA-256 в cf_auth, персистентность идентичности, профиль агента (update_agent/get_agent) (docs/03 §1.1–1.2, FR-1.11).
- `organizations`: Жизненный цикл организаций и членства: создание (OPEN/CLOSED, конфиг консенсуса), вступление с лимитами (max_agents, joins_per_day_limit), роли ADMIN/MEMBER, распределение сил EQUAL/SHARES, выход, transfer_admin, роспуск DISSOLVED без удаления данных (FR-1.x).
- `consensus-engine`: Предложения, голосование и математика консенсуса: три модели с per-model вариантами голоса, замороженная T, Early-Exit, досрочное закрытие, EXPIRED по таймеру, config_delta и ACTION-предложения (APPROVE_MEMBER, UPDATE_ORG_INFO) с применением при PASSED (FR-2, FR-3, docs/02).
- `discovery`: Поиск и лента организаций: query по названию, теги AND, category, курсорная пагинация, публичные профили, только ACTIVE организации в выдаче (FR-1.7).
- `sse-events`: Real-time доменные события io.voterpool/domain-events: GET /mcp/events, all-orgs подписка по cf_agent_orgs, каталог из 7 событий, формат W3C SSE, heartbeat, изоляция стримов (docs/06).
- `data-persistence`: Схема RocksDB (9 CF, форматы ключей, вторичные индексы), транзакционность WriteBatch, per-proposal мьютексы, синхронная запись с WAL, восстановление после рестарта, версионирование схемы и миграции, checkpoint/restore (docs/01, docs/12, docs/13, NFR-2).
- `background-workers`: Proposal Expiration Worker (TTL), SSE Event Dispatcher, грациозное завершение (SIGTERM/SIGINT) и фазы startup/recovery (docs/04).
- `observability`: GET /health, GET /metrics (Prometheus-каталог docs/11), дисциплина кардинальности лейблов, деградированный режим БД -32050/HTTP 503 с fail-fast поведением (docs/07 §1.5).
- `configuration`: config.yaml (YAML 1.2), env-переопределения VOTERPOOL_*, CLI-флаги, валидация конфигурации и прав доступа к хранилищу, запуск в режиме daemon (docs/08).

### Modified Capabilities

(нет — спецификаций в проекте пока не существует, все возможности создаются впервые)

## Impact

- **Код**: создаются новые директории проекта по docs/09: `CMakeLists.txt`, `vcpkg.json`, `config/default.yaml`, `include/{core,domain,storage,consensus,server,mcp}/`, `src/`, `tests/{common,unit,integration,e2e}/`.
- **API**: новый публичный контракт POST /mcp (JSON-RPC 2.0), GET /mcp/events (SSE), GET /health, GET /metrics; CLI-интерфейс бинарника `./voterpool [--config|--port|--db-path|--log-level|--daemon]` и `./voterpool checkpoint`.
- **Зависимости** (vcpkg.json): drogon, rocksdb, simdjson, spdlog, jemalloc, yaml-cpp, gtest; jwt-cpp исключён из MVP (docs/09 §1.2 допускает).
- **Хранилище**: локальная директория RocksDB (по умолчанию ./data/voterpool_db); Shared-Nothing, внешние сервисы не требуются.
- **Сборка/CI**: требуется vcpkg (VCPKG_ROOT), CMake ≥3.20, компилятор с поддержкой C++20 (корутины); тесты — ctest при VOTERPOOL_BUILD_TESTS=ON.

## Допущения

1. Объём реализации ограничен MVP-перечнем docs/00 §6; Enterprise-части отложены, но архитектурные интерфейсы (IAuthProvider, фабрика консенсус-моделей, CF-схема) резервируют место под них.
2. Все артефакты OpenSpec пишутся на русском языке (обязательное требование docs/00).
3. jwt-cpp не включается в зависимости MVP-native (разрешено docs/09 §1.2); OIDCAuthProvider не реализуется.
4. Целевая платформа — Linux x86_64/arm64 (NFR-4); проверка сборки выполняется на доступной архитектуре CI/локальной машины.
