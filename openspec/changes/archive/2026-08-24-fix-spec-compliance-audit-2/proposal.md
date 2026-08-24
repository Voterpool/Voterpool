# Fix spec compliance audit round 2

## Why

Второй раунд аудита specs↔код выявил 9 подтверждённых дефектов: невалидный каталог JSON Schema в tools/list ломает агентов со strict-валидаторами; параметры TLS/лимита тела/таймаута читаются конфигом, но не применяются к серверу; миграция v1→v2 падает на битой записи из-за разыменования пустого optional; shutdown не прекращает приём соединений перед рассылкой server_shutdown; middleware перехватывает неразборчивые тела с -32600 вместо -32700/400; leave_organization для не-участника возвращает -32002 вместо -32004; наблюдаемость врёт (HELP не пишется, early_exit растёт на таймерных закрытиях, agents_total — counter с нулём после рестарта); env-override покрывает ~13 ключей вместо «любого параметра»; SSE-ответ не содержит обязательный заголовок Connection: keep-alive.

## What Changes

- **TLS для Self-Hosted**: `server.ssl` становится полноценно работающей опцией On-Premises-редакции (**BREAKING** для docs: пометка «(Enterprise)» снимается): ssl.enabled=true поднимает HTTPS/SSE-листенер с cert/key; отсутствующие/нечитаемые файлы сертификатов — фатальный отказ старта (код 1).
- **Транспортные лимиты применяются**: max_request_body_size → клиентский лимит размера тела HTTP; request_timeout_sec → таймаут простоя соединения (SSE-стримы переживают за счёт heartbeat).
- **Env-override полон**: любой параметр config.yaml переопределяется VOTERPOOL_{SECTION}_{KEY}, включая вложенные (ssl.cert_path, oidc.*, logging.format/async/async_queue_size, rate_limit.*, storage.*).
- **Валидный inputSchema** (**BREAKING** для клиентов, парсивших плоские строки): properties каждого инструмента — настоящие схемы `{"type": "..."}` draft 2020-12, а не строковые литералы.
- **Parse-error проходит до хендлера**: неразборчивое тело с любым Mcp-Method доходит до обработчика и получает -32700 + HTTP 400; middleware больше не режет такие запросы с -32600.
- **leave_organization для не-участника → -32004**: код приводится к требованиям organizations и docs/05 §1.13 (сами спеки/docs уже согласованы).
- **SSE заголовок Connection: keep-alive** добавлен к ответу /mcp/events (4-й обязательный заголовок).
- **Наблюдаемость честная**: # HELP печатается перед # TYPE с реальными описаниями (тексты из docs/11 §3); voterpool_consensus_early_exit_total инкрементируется только на vote-path Early-Exit (REJECTED по невозможности PASSED), таймерные EXPIRED/REJECTED не считаются; voterpool_agents_total экспонируется как gauge и инициализируется числом агентов из БД при старте.
- **Миграция v1→v2 разделяет два случая**: запись — валидный JSON без config_at_creation → штатная миграция (дописать поле, остальное как есть); запись — не JSON → фатальный отказ: критический лог с ключом записи, версия схемы НЕ повышается, диск не трогается, процесс завершается кодом 1 до подъёма сервера (восстановление из checkpoint вручную; после рестарта миграция идёт заново, уже обновлённые записи не трогаются).
- **Грациозное завершение с draining-флагом**: по SIGTERM/SIGINT первым делом включается draining-режим — новые POST /mcp мгновенно отклоняются HTTP 503 (/health и /metrics доступны), затем server_shutdown → drain воркеров → DB close → logger flush → exit 0.

## Capabilities

### New Capabilities

(нет)

### Modified Capabilities

- `configuration`: новое требование «Применение транспортных параметров» — TLS-листенер, лимит тела запроса и таймаут простоя реально применяются к HTTP-серверу; невалидная SSL-конфигурация фатальна на старте.
- `mcp-protocol`: требование «Метод tools/list» фиксирует форму схем (каждое свойство — объект-схема с type); требование маппинга ошибок дополняется гарантией прохождения parse-error до хендлера независимо от заголовков MCP.
- `observability`: новое требование «Формат экспозиции и семантика метрик» — порядок HELP→TYPE, наличие HELP у каждого семейства, семантика early_exit (только vote-path) и agents_total (gauge от состояния БД).
- `data-persistence`: требование «Идемпотентные возобновляемые миграции» разделяет старую-но-валидную запись (мигрировать) и битую запись (не-JSON → фатальный отказ без повышения версии и изменения диска).
- `background-workers`: требование «Грациозное завершение» уточняет первую фазу: после сигнала новые HTTP/MCP-запросы отклоняются немедленно (HTTP 503, draining), /health и /metrics остаются доступными до выхода.

Примечание: фиксы leave_organization (-32004), SSE-заголовка Connection и полноты env-override приводят код в соответствие с уже существующими требованиями — дельты требований не нужны, изменение чисто кодовое (+тесты).

## Impact

- **Код**: `Config.cpp` (env-маппинг, SSL-валидация путей), `VoterpoolApp.cpp` (addListener useSSL + setSSLFiles, setClientMaxBodySize, setIdleConnectionTimeout, Connection-заголовок SSE, draining-флаг в pre-handling advice и signal handler), все файлы `src/mcp/tools/*.cpp` + `ToolRegistry.{h,cpp}` (schemaObject с дескрипторами типов), `AuthMiddleware.cpp` (passthrough неразборчивых тел), `OrgGovernance.cpp` (-32004), `Metrics.cpp` + `AgentRepository.cpp` + `ConsensusEngine.cpp` + `AppContext.cpp` (HELP/TYPE, early_exit, agents_total gauge + инициализация), `SchemaVersion.cpp` (разделение случаев миграции).
- **Docs**: docs/08-app-config.md — снять пометку «(Enterprise)» с секции ssl, описать применение max_request_body_size/request_timeout_sec; docs/11 — без изменений (уже согласован); README — TLS-пример запуска.
- **Тесты**: новые unit/integration/e2e кейсы на все 9 дефектов; усиление test_protocol.cpp (валидация формы inputSchema, а не только isObject).
- **Совместимость**: клиенты, полагавшиеся на plaintext при ssl.enabled=true или парсившие строковые properties, сломаются осознанно; данные БД не мигрируют новой версией схемы (шаг v1→v2 сохраняет семантику для валидных записей).
