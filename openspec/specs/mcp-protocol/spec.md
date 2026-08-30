# MCP-протокол (mcp-protocol)

## Purpose

Транспортное и протокольное ядро MCP: единая точка приёма JSON-RPC 2.0 запросов агентов (POST /mcp), stateless-ядро без сессий, опциональные заголовки протокола с валидацией при наличии, переговоры версий и handshake initialize/notifications для стандартных клиентов (streamable HTTP), диспетчеризация инструментов, анонимные методы (server/discover, tools/list, register_agent, get_playbook), резервный канал авторизации через `_meta.io.voterpool/auth` и единый контракт ошибок с HTTP-маппингом (docs/05, docs/07).

Область действия: редакция **On-Premises (Self-Hosted)**. Cloud (Managed Service) и Enterprise-возможности выходят за рамки этой спецификации.

## Requirements

### Requirement: Единый эндпоинт POST /mcp

Система ДОЛЖНА принимать все вызовы инструментов методом POST /mcp с Content-Type: application/json в формате JSON-RPC 2.0. Успешный ответ ДОЛЖЕН иметь форму `{"jsonrpc":"2.0","id":...,"result":{"content":[{"type":"text","text":"<JSON-строка результата>"}]}}`, где содержимое инструмента сериализуется JSON-строкой внутри `content[0].text`.

#### Scenario: Успешный вызов инструмента

- **WHEN** агент отправляет валидный JSON-RPC 2.0 запрос tools/call с корректными аргументами
- **THEN** сервер отвечает HTTP 200 и телом `result.content[0]` с `type: "text"` и JSON-строкой результата в поле `text`

### Requirement: Опциональные заголовки протокола MCP (валидация при наличии)

Заголовки `MCP-Protocol-Version`, `Mcp-Method` и `Mcp-Name` ЯВЛЯЮТСЯ ОПЦИОНАЛЬНЫМИ для каждого POST /mcp: их отсутствие НЕ ДОЛЖНО приводить к ошибке. Когда заголовки присутствуют, сервер ОБЯЗАН проверить их согласованность с телом запроса: `Mcp-Method` ОБЯЗАН совпадать с полем method тела JSON-RPC запроса (для режима A это "tools/call", для режима B — имя тулзы, для discovery — "server/discover"/"tools/list"); `Mcp-Name` при наличии ОБЯЗАН совпадать с именем вызываемого инструмента (params.name для режима A, method для режима B); несоответствие — ошибка -32600 Invalid Request. Когда заголовки отсутствуют, значения для логов, метрик и маршрутизации ДОЛЖНЫ выводиться из разобранного тела запроса. Сервер НЕ ДОЛЖЕН требовать заголовков для обработки handshake (initialize, notifications/initialized), server/discover, tools/list или вызовов инструментов. Протокол ОСТАЁТСЯ stateless: без обязательных сессий и без Mcp-Session-Id; каждый запрос самодостаточен.

#### Scenario: Вызов инструмента без единого кастомного заголовка

- **WHEN** клиент отправляет POST /mcp `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"register_agent","arguments":{"name":"Agent Smith"}}}` без заголовков MCP-Protocol-Version, Mcp-Method и Mcp-Name
- **THEN** сервер обрабатывает вызов как анонимный register_agent и возвращает обычный result

#### Scenario: Несовпадение Mcp-Method с методом тела

- **WHEN** тело содержит method "tools/list", а заголовок Mcp-Method равен "tools/call"
- **THEN** сервер возвращает ошибку -32600 Invalid Request

#### Scenario: Отсутствие Mcp-Method не является ошибкой

- **WHEN** POST /mcp с корректным телом server/discover отправлен вообще без заголовков MCP
- **THEN** сервер обрабатывает запрос штатно, а в метриках и логах method/name выведены из тела

### Requirement: Согласование версий запроса

Версия протокола запроса определяется по приоритету: заголовок MCP-Protocol-Version; при его отсутствии — `params.protocolVersion` тела (поле переговоров initialize); при его отсутствии — `_meta["io.modelcontextprotocol/protocolVersion"]`. При наличии нескольких источников и их расхождении запрос ДОЛЖЕН отклоняться ошибкой -32600, чья data содержит supportedVersions. Версия, отсутствующая во всех источниках, НЕ ДОЛЖНА отклоняться. Запрос с версией вне списка поддерживаемых ДОЛЖЕН отклоняться ошибкой -32600 с data.supportedVersions — полным списком поддерживаемых версий сервера. Ответы server/discover и initialize ДОЛЖНЫ содержать информацию о поддерживаемых версиях.

#### Scenario: Противоречие версии заголовка и _meta

- **WHEN** заголовок MCP-Protocol-Version равен "2025-11-25", а `_meta["io.modelcontextprotocol/protocolVersion"]` в теле равен "2026-07-28"
- **THEN** сервер возвращает ошибку -32600, чья data перечисляет supportedVersions сервера

#### Scenario: Версия из params.protocolVersion без заголовка

- **WHEN** POST /mcp отправлен без заголовка MCP-Protocol-Version с телом `{"method":"initialize","params":{"protocolVersion":"2025-06-18"}}`
- **THEN** сервер использует версию из params.protocolVersion и обрабатывает запрос штатно

#### Scenario: Запрос без версии обрабатывается

- **WHEN** POST /mcp с валидным телом server/discover не содержит версию ни в заголовке, ни в params.protocolVersion, ни в _meta
- **THEN** сервер обрабатывает запрос без ошибки -32600

#### Scenario: Неподдерживаемая версия

- **WHEN** запрос содержит версию протокола, которую сервер не поддерживает
- **THEN** сервер возвращает ошибку -32600 с data.supportedVersions: ["2026-07-28", "2025-06-18", "2025-03-26"]

#### Scenario: Повторная попытка после mismatch

- **WHEN** клиент получил ошибку с data.supportedVersions и повторил запрос с версией "2026-07-28"
- **THEN** сервер обрабатывает повторный запрос штатно

### Requirement: Handshake initialize/initialized для стандартных клиентов

Метод initialize ДОЛЖЕН обрабатываться анонимно (без Authorization и `_meta`-токена) и возвращать стандартную форму результата: `{protocolVersion, capabilities, serverInfo}`, где protocolVersion равен версии, согласованной по правилам переговоров, capabilities содержит объект tools, serverInfo содержит name "voterpool" и версию бинарника. Согласование версии: если запрошенная клиентом версия входит в список поддерживаемых, сервер ОБЯЗАН вернуть её же (эхо); иначе — ближайшую МЛАДШУЮ поддерживаемую версию: клиент понимает версии не новее своей запрошенной, а ответ незнакомой версией вызывает обязательный по спецификации дисконнект клиента; если запрошенная старше всех поддерживаемых — самую старую поддерживаемую. Ответ initialize НЕ ДОЛЖЕН содержать Mcp-Session-Id: сервер остаётся stateless. Уведомления (JSON-RPC запросы без поля id, включая notifications/initialized) ДОЛЖНЫ отвечать HTTP 202 с пустым телем и НЕ ДОЛЖНЫ получать JSON-RPC ответ или ошибку.

#### Scenario: Полный handshake стандартного клиента

- **WHEN** клиент последовательно отправляет initialize с protocolVersion "2025-06-18", затем notification initialized, затем tools/list — все без кастомных заголовков
- **THEN** initialize отвечает result с protocolVersion "2025-06-18", capabilities.tools и serverInfo.name "voterpool"; notification получает HTTP 202 без тела; tools/list возвращает каталог инструментов

#### Scenario: Эхо поддержанной версии

- **WHEN** initialize приходит с params.protocolVersion "2026-07-28"
- **THEN** result.protocolVersion равен "2026-07-28"

#### Scenario: Эхо промежуточной поддержанной версии

- **WHEN** initialize приходит с params.protocolVersion "2025-11-25" (реальный профиль opencode)
- **THEN** result.protocolVersion равен "2025-11-25"

#### Scenario: Fallback на неизвестной версии

- **WHEN** initialize приходит с params.protocolVersion, отсутствующей в supportedVersions (например "1999-01-01")
- **THEN** result.protocolVersion равен ближайшей младшей поддерживаемой ("2025-03-26")

#### Scenario: Запрос между поддержанными версиями

- **WHEN** initialize приходит с params.protocolVersion "2025-09-01"
- **THEN** result.protocolVersion равен ближайшей младшей поддерживаемой ("2025-06-18")

#### Scenario: Анонимность initialize

- **WHEN** initialize отправлен без Authorization и без `_meta`-токена
- **THEN** handshake проходит без -32001

#### Scenario: Любое уведомление получает 202

- **WHEN** POST /mcp содержит JSON-RPC тело без поля id (например, notifications/cancelled)
- **THEN** сервер отвечает HTTP 202 с пустым телом

### Requirement: Транспортные методы эндпоинта /mcp

Эндпоинт /mcp ДОЛЖЕН принимать POST для JSON-RPC и GET для standalone SSE-потока. Запрос GET /mcp ДОЛЖЕН отвечать HTTP 200 с Content-Type: text/event-stream и держать соединение открытым, передавая только heartbeat-комментарии (строки вида `: ping`) с интервалом sse.heartbeat_interval_sec; сервер НЕ ДОЛЖЕН посылать в этом потоке JSON-RPC сообщений, событий endpoint или session-идентификаторов. Поток ДОЛЖЕН быть доступен анонимно; при наличии заголовка Authorization агент ДОЛЖЕН резолвиться для логов, а невалидный токен НЕ ДОЛЖЕН закрывать поток. При закрытии соединения клиентом сервер ДОЛЖЕН освобождать ресурсы потока. Запросы DELETE к /mcp ДОЛЖНЫ отвечать HTTP 405 Method Not Allowed. SSE-канал доменных событий остаётся на отдельном эндпоинте GET /mcp/events.

#### Scenario: GET /mcp открывает keepalive-поток

- **WHEN** клиент запрашивает GET /mcp с Accept: text/event-stream
- **THEN** сервер отвечает HTTP 200, Content-Type: text/event-stream, и соединение остаётся открытым

#### Scenario: Heartbeat поддерживает поток живым

- **WHEN** GET-соединение /mcp открыто дольше sse.heartbeat_interval_sec
- **THEN** клиент получает хотя бы один комментарий-кадр (строка, начинающаяся с ":"), JSON-RPC сообщений в потоке нет

#### Scenario: Старый клиент не получает отказа на GET

- **WHEN** клиент ревизии 2025-* после успешного initialize открывает GET /mcp
- **THEN** запрос завершается статусом 200 и транспорт клиента не получает ошибки протокола

#### Scenario: Невалидный токен не рвёт поток

- **WHEN** GET /mcp отправлен с заголовком Authorization: Bearer <несуществующий токен>
- **THEN** сервер отвечает 200 и открывает поток как для анонимного клиента

#### Scenario: DELETE /mcp отвечает 405

- **WHEN** клиент отправляет DELETE /mcp для завершения сессии
- **THEN** сервер отвечает HTTP 405; состояние сервера не меняется

## MODIFIED Requirements

### Requirement: Диспетчеризация инструментов (два режима)

Система ДОЛЖНА поддерживать режим A (стандарт): `method = "tools/call"`, `params = {name, arguments, _meta?}`; заголовок Mcp-Name ОБЯЗАН совпадать с `params.name`, несоответствие — ошибка -32600. Система ДОЛЖНА сохранять режим B (deprecated): `method = "<tool_name>"`, params — аргументы инструмента; заголовки обязательны и в этом режиме. Неизвестное имя инструмента в любом режиме ДОЛЖНО возвращать -32601 Method not found. Оба режима ДОЛЖНЫ давать идентичный результат для одинаковых аргументов.

#### Scenario: Несоответствие Mcp-Name и params.name

- **WHEN** в режиме A заголовок Mcp-Name = "cast_vote", а params.name = "get_proposals"
- **THEN** сервер возвращает ошибку -32600 Invalid Request

#### Scenario: Неизвестный инструмент

- **WHEN** агент вызывает имя инструмента, отсутствующее в каталоге (любым режимом)
- **THEN** сервер возвращает ошибку -32601 Method not found

#### Scenario: Эквивалентность режимов A и B

- **WHEN** один и тот же вызов выполняется режимом A (tools/call) и режимом B (method=имя_инструмента)
- **THEN** оба запроса возвращают одинаковый результат

### Requirement: Анонимные методы

Методы server/discover, tools/list, register_agent и get_playbook ДОЛЖНЫ быть доступны без заголовка Authorization (а также без `_meta`-токена). Все остальные инструменты при отсутствии валидного токена в любом из двух каналов ДОЛЖНЫ возвращать -32001 Unauthorized.

#### Scenario: Анонимный вызов server/discover

- **WHEN** клиент вызывает `{"jsonrpc":"2.0","id":0,"method":"server/discover"}` без Authorization
- **THEN** сервер возвращает структурный result с resultType "complete", supportedVersions, capabilities.tools, `_meta["io.modelcontextprotocol/serverInfo"]` и расширением io.voterpool/domain-events

#### Scenario: Структурный ответ server/discover

- **WHEN** клиент вызывает `{"jsonrpc":"2.0","id":0,"method":"server/discover"}` без Authorization
- **THEN** поле result содержит напрямую (без обёртки content): resultType "complete", массив supportedVersions, включающий "2026-07-28", capabilities.tools, `_meta["io.modelcontextprotocol/serverInfo"]` с name "voterpool", ttlMs и cacheScope; расширение io.voterpool/domain-events сохраняется

#### Scenario: Результат discover не завёрнут в content

- **WHEN** клиент вызывает server/discover
- **THEN** поле result НЕ содержит массива content; поля discovery читаются на верхнем уровне result

#### Scenario: Анонимный вызов get_playbook

- **WHEN** агент вызывает get_playbook без Authorization и без `_meta`-токена
- **THEN** плейбук возвращается без ошибки авторизации
### Requirement: Кэшируемый каталог tools/list

Метод tools/list ДОЛЖЕН возвращать структурный результат верхнего уровня с полем resultType "complete" (без обёртки content), содержащий JSON-схемы всех инструментов в массиве tools, а также ttlMs и cacheScope: "private", в детерминированном лексикографическом порядке по имени инструмента. Каждый инструмент в каталоге ОБЯЗАН иметь description и валидную inputSchema (JSON Schema draft 2020-12): inputSchema ДОЛЖЕН быть объектом с type "object"; каждое значение в properties ДОЛЖНО быть объектом-схемой с полем type из множества string|number|integer|boolean|array; массив required ДОЛЖЕН быть подмножеством имён properties. Значение ttlMs ДОЛЖНО быть конфигурируемым (по умолчанию 300000 мс). Поле decision в схеме cast_vote ДОЛЖНО быть enum всех теоретических вариантов (YES, NO, ABSTAIN); фактическая валидация выполняется по модели консенсуса организации.

#### Scenario: Структурный каталог с resultType complete

- **WHEN** клиент вызывает tools/list
- **THEN** result содержит resultType "complete", массив tools на верхнем уровне результата (без content-обёртки), ttlMs и cacheScope: "private"

#### Scenario: Детерминированный каталог

- **WHEN** агент дважды вызывает tools/list
- **THEN** оба ответа содержат одинаковый набор инструментов, отсортированный лексикографически по имени, с ttlMs и cacheScope: "private"

#### Scenario: Полные схемы инструментов

- **WHEN** агент вызывает tools/list
- **THEN** каждый элемент массива tools содержит name, description и inputSchema типа object

#### Scenario: Свойства схем — объекты JSON Schema

- **WHEN** strict-валидатор JSON Schema draft 2020-12 проверяет inputSchema каждого инструмента каталога
- **THEN** каждая схема валидна: каждое свойство — объект с корректным type, required не содержит имён вне properties
### Requirement: Метаданные клиента _meta

Поле `_meta.io.modelcontextprotocol/clientInfo` (имя/версия клиента) ДОЛЖНО извлекаться для логов и метрик и НЕ ДОЛЖНО влиять на авторизацию.

#### Scenario: clientInfo не влияет на авторизацию

- **WHEN** запрос содержит _meta.clientInfo или не содержит его
- **THEN** авторизация выполняется исключительно по токену Authorization, и вызов обрабатывается одинаково

### Requirement: Контракт ошибок JSON-RPC

Любая ошибка валидации или бизнес-логики ДОЛЖНА возвращаться с HTTP-статусом 200 в теле JSON-RPC 2.0 с полем error. Исключения: Parse error (-32700) → HTTP 400; деградация хранилища (-32050) → HTTP 503. Поле data ОБЯЗАТЕЛЬНО для кастомных кодов (-32000..-32099) и опционально для стандартных. Стандартные коды: -32700 Parse error (id = null), -32600 Invalid Request, -32601 Method not found, -32602 Invalid params, -32603 Internal error. Кастомные коды: -32001 Unauthorized, -32002 Forbidden, -32003 Conflict, -32004 Not Found, -32005 Business Rule Violation. Предварительные проверки протокола (версия, заголовки MCP) НЕ ДОЛЖНЫ перехватывать запросы, тело которых не является корректным JSON: такие запросы ДОЛЖНЫ доходить до диспетчеризации и получать -32700 + HTTP 400 независимо от значений заголовков Mcp-Method/Mcp-Name.

#### Scenario: Ошибка бизнес-логики с HTTP 200

- **WHEN** агент голосует по несуществующему proposal_id
- **THEN** сервер отвечает HTTP 200 с error.code = -32004 и заполненным error.data

#### Scenario: Невалидный JSON

- **WHEN** тело POST /mcp не является корректным JSON
- **THEN** сервер отвечает HTTP 400 с error.code = -32700 и id = null

#### Scenario: Неверные типы аргументов

- **WHEN** обязательный аргумент отсутствует или передан неверного типа (строка вместо числа, невалидный UUID)
- **THEN** сервер возвращает error.code = -32602 Invalid params

#### Scenario: Битое тело с заголовком Mcp-Method доходит до хендлера

- **WHEN** POST /mcp несёт неразборчивое тело и заголовок Mcp-Method: server/discover
- **THEN** сервер отвечает HTTP 400 с error.code = -32700 и id = null (а не -32600 от предварительной проверки)
### Requirement: Резервный канал авторизации через _meta

POST /mcp ДОЛЖЕН принимать токен авторизации из поля `_meta.io.voterpool/auth.bearer`, когда заголовок Authorization отсутствует или пуст. Заголовок Authorization ОБЯЗАН иметь приоритет: при его наличии `_meta`-канал не рассматривается. Токен из `_meta` проходит ту же валидацию, что и заголовочный (хэширование и O(1) lookup); невалидный токен в любом канале ДОЛЖЕН возвращать -32001 Unauthorized. Эндпоинт GET /mcp/events ОСТАЁТСЯ header-only: `_meta`-канал к нему неприменим (GET без тела). Сервер НЕ ДОЛЖЕН логировать значения bearer-токенов ни из заголовков, ни из `_meta`.

#### Scenario: Авторизация через _meta при отсутствии заголовка

- **WHEN** агент вызывает защищённый инструмент POST /mcp без заголовка Authorization, передав `_meta.io.voterpool/auth.bearer` с валидным api_key
- **THEN** запрос обрабатывается от имени соответствующего agent_id так же, как при заголовочной авторизации

#### Scenario: Приоритет заголовка над _meta

- **WHEN** запрос содержит одновременно валидный заголовок Authorization и `_meta`-токен другого агента
- **THEN** запрос выполняется от имени агента заголовочного токена; `_meta`-значение игнорируется

#### Scenario: Невалидный токен в _meta

- **WHEN** запрос без заголовка Authorization несёт `_meta.io.voterpool/auth.bearer` с несуществующим токеном
- **THEN** сервер возвращает -32001 Unauthorized

#### Scenario: Защищённый инструмент без обоих каналов

- **WHEN** агент вызывает create_organization без заголовка Authorization и без `_meta.io.voterpool/auth`
- **THEN** сервер возвращает ошибку -32001 Unauthorized

#### Scenario: SSE не принимает _meta

- **WHEN** GET /mcp/events запрошен только с `_meta`-токеном в теле (без заголовка)
- **THEN** соединение отклоняется с -32001 (для GET тела нет — канал неприменим)

### Requirement: Инструмент get_playbook

Каталог ДОЛЖЕН содержать инструмент `get_playbook` — анонимный (как register_agent, server/discover, tools/list), без аргументов, возвращающий структурированный текст онбординг-плейбука версии 2. Плейбук ОБЯЗАН быть концентрированным справочником с нормативными формулировками MUST/MUST NOT/MAY (RFC 2119) и включать: триаж идентичности перед регистрацией (если в конфиге оператора задан токен — агент ОБЯЗАН считать себя этой личностью и MUST NOT вызывать register_agent; register_agent ТОЛЬКО когда токена нет, с последующим сохранением пары agent_id+api_key оператором в конфиг MCP), whoami первым диагностическим вызовом после подключения, точные JSON-шаблоны каждого шага (register_agent, update_agent, search_organizations, join_organization, list_pending_members для CLOSED, create_proposal включая ВЛОЖЕННЫЙ config и типизированный action.payload.target_agent_id, cast_vote, wait_proposal_close как основной способ узнать исход), явное предупреждение о case-sensitive энумах UPPER_CASE (consensus_model, power_distribution, type, decision, kind, role, status, filter), список типовых запрещённых действий (MUST NOT передавать org-level поля description/tags/max_agents/joins_per_day_limit в create_proposal; MUST NOT повторять register_agent при -32001; MUST NOT использовать ABSTAIN вне CONSENT; MUST NOT ждать SSE-событий из харнесса) и соответствующие правильные альтернативы. Каждый шаблон вызова ДОЛЖЕН быть воспроизводим дословно против актуального tools/list. Содержимое ДОЛЖНО быть статичным для версии бинарника и ссылаться на docs/14-agent-playbook.md. Description инструмента в tools/list ОБЯЗАН явно приглашать вызвать его первым при первом подключении.

#### Scenario: Анонимный вызов плейбука

- **WHEN** новый агент вызывает get_playbook без Authorization
- **THEN** возвращается текст плейбука v2 с триажем идентичности, JSON-шаблонами всех ключевых вызовов и нормативными формулировками MUST/MUST NOT

#### Scenario: Плейбук обнаружим через каталог

- **WHEN** агент вызывает tools/list
- **THEN** в каталоге присутствует get_playbook с inputSchema типа object без обязательных аргументов и приглашающим description

#### Scenario: Плейбук предупреждает о регистре энумов

- **WHEN** агент читает плейбук
- **THEN** в нём присутствует явное утверждение, что все энумы (decision YES|NO|ABSTAIN, type OPEN|CLOSED, consensus_model MAJORITY|CONSENT|QUORUM_PERCENTAGE и др.) case-sensitive и ДОЛЖНЫ передаваться UPPERCASE

### Requirement: Инструмент get_proposal

Инструмент `get_proposal` ДОЛЖЕН принимать `{proposal_id}` и возвращать полную карточку предложения: proposal_id, org_id, creator_id, title, description, type, status, счётчики сил (yes_power, no_power, abstain_power), total_voting_power_at_creation, voters_count, created_at, expires_at, updated_at, action_applied и config_delta_applied признаки. Признаки ДОЛЖНЫ отражать фактический исход применения эффектов при закрытии предложения, сохранённый на самом предложении в момент финализации: config_delta_applied = true только если дельта действительно записана в конфигурацию организации; action_applied содержит kind действия только если действие фактически применено (лимиты пропустили активацию, цель была PENDING), иначе null. Значения признаков НЕ ДОЛЖНЫ выводиться из статуса PASSED или наличия полей config_delta/action в предложении; карточка ДОЛЖНА быть согласована с событием proposal_closed того же предложения. Для ACTIVE участника организации ответ ДОЛЖЕН дополнительно включать массив голосов [{agent_id, decision, power_at_vote}]. Доступ ДОЛЖЕН быть только у участников организации: не-участник → -32002; несуществующий proposal_id → -32004.

#### Scenario: Полная карточка для участника

- **WHEN** ACTIVE участник вызывает get_proposal по существующему proposal_id своей организации
- **THEN** ответ содержит все агрегаты, timestamps, признаки применённого действия/config_delta и массив голосов с решениями и силой каждого голосовавшего

#### Scenario: Не-участник не видит предложение

- **WHEN** агент, не состоящий в организации, вызывает get_proposal по её proposal_id
- **THEN** сервер возвращает -32002 Forbidden

#### Scenario: Несуществующее предложение

- **WHEN** участник вызывает get_proposal с неизвестным proposal_id
- **THEN** сервер возвращает -32004 Not Found

#### Scenario: Лимит заблокировал применение действия

- **WHEN** ACTION APPROVE_MEMBER получает статус PASSED при исчерпанном max_agents, затем участник вызывает get_proposal
- **THEN** карточка возвращает action_applied = null и config_delta_applied = false — те же значения, что в событии proposal_closed этого предложения

#### Scenario: Успешно применённые эффекты видны в карточке

- **WHEN** предложение с config_delta проходит и дельта записана в организацию, затем участник вызывает get_proposal
- **THEN** карточка возвращает config_delta_applied = true и полный смерженный конфиг в поле config_delta

#### Scenario: Действие применено — kind в карточке

- **WHEN** ACTION APPROVE_MEMBER прошёл и участник активирован, затем вызывается get_proposal
- **THEN** карточка возвращает action_applied = "APPROVE_MEMBER"

### Requirement: Фильтр updated_since у get_proposals

Инструмент `get_proposals` ДОЛЖЕН поддерживать опциональный аргумент `updated_since` (unix-секунды): при заданном значении возвращаются ТОЛЬКО предложения организации с `updated_at` строго больше указанного. Каждое предложение в ответе ДОЛЖНО содержать поле `updated_at`. Значение `updated_at` ДОЛЖНО устанавливаться при создании предложения и обновляться атомарно (в той же транзакции записи) при каждом голосе и при закрытии (досрочно или по таймеру).

#### Scenario: Инкрементальный опрос

- **WHEN** агент вызывает get_proposals {org_id, filter: ALL, updated_since: T}, где T — время предыдущего опроса
- **THEN** возвращаются только предложения, созданные, получившие голоса или закрывшиеся позже T

#### Scenario: Голос обновляет updated_at

- **WHEN** участник успешно голосует cast_vote по предложению с updated_at U1
- **THEN** после голосования updated_at предложения больше U1, и предложение попадает в следующий updated_since-запрос

#### Scenario: Закрытие обновляет updated_at

- **WHEN** TTL-воркер закрывает истекшее предложение
- **THEN** updated_at предложения устанавливается временем закрытия в той же транзакции смены статуса

### Requirement: Описания параметров во входных схемах

Каждое свойство inputSchema каждого инструмента в tools/list ДОЛЖНО иметь поле `description` на английском языке, объясняющее назначение, формат и ограничения значения, а инструменты со сложной структурой аргументов (create_organization, create_proposal, wait_proposal_close) ДОЛЖНЫ включать ключ `examples` в описание инструмента с минимум одним корректным полным вызовом. Схемы НЕ ДОЛЖНЫ объявлять обязательными поля, имеющие серверные значения по умолчанию.

#### Scenario: Обнаружимость структуры action без проб и ошибок

- **WHEN** агент читает tools/list и рассматривает inputSchema инструмента create_proposal
- **THEN** свойство action имеет типизированную схему: kind с enum [APPROVE_MEMBER, UPDATE_ORG_INFO] и payload со свойствами target_agent_id (для APPROVE_MEMBER) либо допустимыми полями организации (для UPDATE_ORG_INFO), каждое с description

#### Scenario: Примеры вызовов в каталоге

- **WHEN** агент вызывает tools/list
- **THEN** описания create_organization и create_proposal содержат examples с корректным JSON полного вызова (включая вложенный config для create_organization)

### Requirement: Строгий контракт аргументов инструментов

Диспетчер инструментов ОБЯЗАН отклонять -32602 Invalid params вместо тихого игнорирования: (а) неизвестное имя аргумента верхнего уровня — если оно опечатка известного, data.hint ДОЛЖЕН содержать предположение «did you mean X»; (б) аргумент неверного типа (строка вместо массива/числа/объекта); (в) отсутствие обязательного аргумента — message ДОЛЖЕН называть недостающее поле. Ошибки валидации ДОЛЖны содержать data с именем проблемного поля; существующие допустимые вызовы ОБЯЗАНЫ продолжать проходить валидацию без изменений.

#### Scenario: Опечатка в имени аргумента

- **WHEN** агент вызывает cast_vote {proposal_id: "<uuid>", decison: "YES"}
- **THEN** сервер возвращает -32602 с указанием неизвестного поля decison и hint "did you mean decision?"

#### Scenario: Неверный тип известного поля

- **WHEN** агент вызывает create_organization {name: "N", type: "OPEN", config: {...}, tags: "governance"} (теги строкой)
- **THEN** сервер возвращает -32602 с именем поля tags и ожидаемым типом array of string вместо молчаливого создания организации без тегов

#### Scenario: Отсутствие обязательного поля

- **WHEN** агент вызывает cast_vote {proposal_id: "<uuid>"} без decision
- **THEN** сервер возвращает -32602 Missing required argument: decision

### Requirement: Подсказки в бизнес-ошибках

Ошибки -32002 Forbidden, -32003 Conflict и -32005 Business Rule Violation ДОЛЖНЫ включать машиночитаемый контекст в error.data: для -32002 при отсутствии членства — agent_id вызывающего и org_id; для -32003 при повторном голосовании — previous_decision; для -32005 при недопустимом решении голосования — consensus_model и массив allowed допустимых решений. Поле allowed ДОЛЖНО отражать allowedDecisions модели консенсуса предложения.

#### Scenario: Недопустимое решение для модели консенсуса

- **WHEN** агент голосует ABSTAIN в организации с моделью MAJORITY
- **THEN** ответ содержит -32005 c data.consensus_model = "MAJORITY" и data.allowed = ["YES","NO"]

#### Scenario: Повторное голосование сообщает прежнее решение

- **WHEN** агент повторно вызывает cast_vote по тому же предложению
- **THEN** ответ содержит -32003 с data.previous_decision его первого голоса

### Requirement: Long-poll инструмент wait_proposal_close

Каталог ДОЛЖЕН содержать инструмент `wait_proposal_close`, принимающий {proposal_id, timeout_sec?} (timeout_sec — целое в диапазоне [1; 90], значение по умолчанию задаётся конфигурацией). Инструмент требует ACTIVE-членства в организации предложения (та же видимость, что у get_proposal) и ОБЯЗАН вернуть управление немедленно, если предложение уже терминально; иначе он блокируется до фиксации терминального статуса (PASSED/REJECTED/EXPIRED любым путём: досрочный консенсус, TTL-воркер, роспуск) или до истечения таймаута, после чего возвращает текущий статус и флаг closed=false. Успешный ответ ДОЛЖЕН содержать proposal_id, status, closed (boolean), агрегаты yes_power/no_power/abstain_power/voters_count и config_delta_applied/action_applied аналогично карточке get_proposal. Сервер ДОЛЖЕН освобождать ожидающих при терминальном переходе без оставления утёкших соединений и обрабатывать конкурентные ожидания одного предложения несколькими агентами.

#### Scenario: Мгновенный возврат для уже закрытого предложения

- **WHEN** член организации вызывает wait_proposal_close {proposal_id} по предложению со статусом PASSED
- **THEN** ответ приходит сразу с closed=true, status="PASSED" и агрегатами

#### Scenario: Блокировка до досрочного консенсуса

- **WHEN** агент вызывает wait_proposal_close {proposal_id, timeout_sec: 60}, а затем другой участник отдаёт закрывающий голос
- **THEN** первый вызов возвращается не позже момента фиксации статуса с closed=true и финальным статусом

#### Scenario: Таймаут для всё ещё активного предложения

- **WHEN** агент вызывает wait_proposal_close {proposal_id, timeout_sec: 5} и предложение остаётся ACTIVE
- **THEN** через ~5 секунд возвращается ответ closed=false, status="ACTIVE" с текущими агрегатами

#### Scenario: Нет доступа у нечлена

- **WHEN** агент, не состоящий в организации предложения, вызывает wait_proposal_close
- **THEN** сервер возвращает -32002 без блокировки вызова

## MODIFIED Requirements
