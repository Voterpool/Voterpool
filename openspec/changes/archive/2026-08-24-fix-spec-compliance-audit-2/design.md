# Design — fix-spec-compliance-audit-2

## Context

Движок C++/Drogon/RocksDB, редакция On-Premises. Аудит выявил 9 дефектов (см. proposal.md — Why). Ключевые точки: конфиг читает транспортные параметры, но `VoterpoolApp::run()` передаёт в Drogon только host/port/threads; `schemaObject` кладёт строковые литералы в properties; `AuthMiddleware::probeBody` не различает «битое тело» от «метода из заголовка»; метрики пишут help=name и печатают TYPE до HELP; `ConsensusEngine::finalizeLocked` инкрементирует early_exit на любом не-PASSED; миграция v1→v2 разыменовывает пустой optional при `!json`; `drainAndQuit` не останавливает приём запросов; `AppContext::init` реагирует только на `kFatalNewerSchema`, а `kError` миграции проходит молча.

Ограничения: публичный API Drogon (vcpkg) без доступа к internals listener-managerа; SSE-стримы живут дольше request_timeout_sec за счёт heartbeat; повторный SIGTERM должен быть безопасен.

## Goals / Non-Goals

**Goals:**

- Все 9 дефектов закрыты, поведение соответствует спекам и docs.
- Каждый фикс покрыт тестом (unit/integration/e2e), полный набор зелёный.
- Docs синхронизированы (docs/08 ssl, README TLS-пример).

**Non-Goals:**

- OIDC-авторизация (остаётся фатальной в On-Premises).
- rate_limit-функциональность (параметры мапятся из env, но остаются неактивными).
- Переименование voterpool_agents_total (суффикс _total закреплён каталогом docs/11 §3).
- Проверка заголовка Accept у /mcp/events (спека требует «принимать», не «требовать»).

## Decisions

### D1. TLS через публичный API Drogon

`ssl.enabled=true` → `drogon::app().setSSLFiles(cert_path, key_path)` + `addListener(host, port, /*useSSL=*/true, cert, key)`. Предварительная проверка существования и читаемости cert/key в `AppConfig::validate()` → ConfigError → exit 1 до открытия порта (тот же путь, что у невалидного портa). Альтернатива «fail-fast без TLS» отклонена решением владельца: TLS нужен как опция Self-Hosted.

### D2. Транспортные лимиты — прямые маппинги Drogon

max_request_body_size → `setClientMaxBodySize()`; request_timeout_sec → `setIdleConnectionTimeout()`. SSE-подписки переживают таймаут простоя: heartbeat каждые sse.heartbeat_interval_sec сбрасывает idle-таймер. Вызов ставится в `VoterpoolApp::run()` рядом с addListener.

### D3. Полная таблица env-override

applyEnv дополняется до покрытия всех полей AppConfig; вложенность сплющивается подчёркиваниями: `VOTERPOOL_SERVER_SSL_CERT_PATH`, `VOTERPOOL_SERVER_SSL_KEY_PATH`, `VOTERPOOL_SERVER_MAX_REQUEST_BODY_SIZE`, `VOTERPOOL_SERVER_REQUEST_TIMEOUT_SEC`, `VOTERPOOL_STORAGE_WRITE_BUFFER_SIZE`, `VOTERPOOL_STORAGE_MAX_WRITE_BUFFER_NUMBER`, `VOTERPOOL_STORAGE_LOG_LEVEL`, `VOTERPOOL_STORAGE_REBUILD_INDEX_ON_START`, `VOTERPOOL_AUTH_OIDC_JWKS_URL|ISSUER|CACHE_TTL_SEC`, `VOTERPOOL_LOGGING_FORMAT|ASYNC|ASYNC_QUEUE_SIZE`, `VOTERPOOL_RATE_LIMIT_ENABLED|RPS_PER_AGENT|RPS_PER_ORG`. Коллизий имён нет (префиксы секций уникальны).

### D4. schemaObject принимает дескрипторы типов

Контракт `schemaObject(properties, required)` сохраняется, но значения properties строятся хелперами (`schemaString()`, `schemaNumber()`, `schemaEnum({...})`, …), возвращающими объекты `{"type": ...}` / `{"type": "string", "enum": [...]}`. Механическая замена во всех ~17 tool-дефах. cast_vote.decision получает enum всех вариантов по требованию спеки.

### D5. Passthrough неразборчивого тела в middleware

В `AuthMiddleware::handle` ветка «probe.method пуст» перестаёт отвергать по Mcp-Method: любое неразборчивое тело проходит к хендлеру (получит -32700 + HTTP 400). Проверки версии протокола остаются только для разобранных тел. Отклонение -32600 сохраняется для валидного JSON с нарушением заголовков (это не parse error).

### D6. Метрики: честные HELP, семантика, типы

- `registerDefaults` хранит описания из docs/11 §3; `expose()` печатает всегда `# HELP` перед `# TYPE`.
- early_exit: инкремент переносится из `finalizeLocked` в vote-path (ветка `eval.finalStatus` при REJECTED); таймер/dissolve пути счётчик не трогают.
- agents_total: переносится из списка counters в gauges; `incCounter` удаляется; `AgentRepository::create` оставляет один `setGauge(count())`; `AppContext::init` после создания репозиториев выставляет gauge из `count()` — рестарт на непустой базе даёт корректное значение с первого скрейпа.

### D7. Миграция v1→v2: два прохода, битая запись фатальна

Проход 1 (validate): скан cf_proposals, каждая запись обязана распарситься; иначе `spdlog::critical` с ключом записи → `migrateTo` возвращает false без единой записи в батч и без writeVersion → диск не изменён. Проход 2 (mutate): текущая пакетная логика дописывания config_at_creation (идемпотентность сохраняется — поле-маркер). Двухпроходность вместо одного прохода со сбором апдейтов гарантирует «на диске всё осталось как было» даже при частично собранном батче. В `AppContext::init` добавляется обработка `kError` → throw → VoterpoolApp::init ловит → критический лог → main возвращает 1. Пустой optional больше нигде не разыменовывается.

### D8. Draining-флаг в signal handler

Атомарный флаг в VoterpoolApp; `drainAndQuit` начинается с `CAS(false→true)` (идемпотентность повторного SIGTERM) → pre-handling advice проверяет флаг после health/metrics-bypass: POST /mcp → мгновенный HTTP 503 без диспетчеризации; GET /health и /metrics продолжают отвечать. Далее прежний порядок: hub->shutdownAll (server_shutdown) → workers->stop (drain очереди) → quit через runAfter. DB close и logger flush остаются в finalizeShutdown/main после выхода из run(). Альтернатива «закрывать TCP-listener через internals Drogon» отклонена: непубличный API, хрупко между версиями.

### D9. leave_organization: -32004 для не-участника

`RpcError::forbidden` заменяется на `RpcError::notFound("Organization", org_id)` — тот же ответ, что у DISSOLVED-организации: не-участник не может отличить «нет членства» от «организация распущена» (не выдаёт статус членства). Спека organizations и docs/05 §1.13 уже требуют -32004 — код приводится к ним, дельта требований не нужна.

## Risks / Trade-offs

- [Drogon SSL требует сборки с OpenSSL] → проверить наличие TLS в vcpkg-установке drogon на этапе задачи; e2e-тест TLS использует self-signed сертификат, генерируемый в tmpdir теста.
- [Idle-timeout рвёт медленных MCP-клиентов с паузами > request_timeout_sec] → значение по умолчанию 30 c из docs/08 сохраняется; heartbeat SSE покрывает подписки; поведение документировано в дельте configuration.
- [Двухпроходная миграция читает CF дважды] → одноразовая операция при апгрейде; объёмы cf_proposals ограничены; компромисс осознанный ради гарантии нетронутого диска.
- [503 в draining может попасть в метрики mcp_requests как error] → решение: draining-отклонение не инкрементирует инструментальные метрики (запрос не диспетчеризуется); это совпадает с трактовкой «не принимаем соединения».
- [Смена типа agents_total ломает дашборды, ожидавшие counter] → имя неизменно, значения монотонно растут при обычной эксплуатации; изменение зафиксировано в CHANGELOG-заметке PR.

## Migration Plan

Без схемы данных новой версии нет; шаг миграции v1→v2 сохраняет семантику для валидных записей и получает фатальный отказ на битых. Развёртывание: остановка → новый бинарник → старт (миграция идёт автоматически). Rollback: предыдущий бинарник поверх (версия схемы уже 2 → фатальный отказ даунгрейда — стандартное ограничение проекта). Для TLS-операторов: выпустить сертификат, включить ssl.enabled, рестарт.

## Open Questions

(нет)
