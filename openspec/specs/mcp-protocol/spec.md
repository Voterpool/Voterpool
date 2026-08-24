# MCP-протокол (mcp-protocol)

## Purpose

Транспортное и протокольное ядро MCP 2026-07-28: единая точка приёма JSON-RPC 2.0 запросов агентов (POST /mcp), обязательные заголовки протокола, диспетчеризация инструментов, анонимные методы (server/discover, tools/list, register_agent, get_playbook), резервный канал авторизации через `_meta.io.voterpool/auth` и единый контракт ошибок с HTTP-маппингом (docs/05, docs/07).

Область действия: редакция **On-Premises (Self-Hosted)**. Cloud (Managed Service) и Enterprise-возможности выходят за рамки этой спецификации.

## Requirements

### Requirement: Единый эндпоинт POST /mcp

Система ДОЛЖНА принимать все вызовы инструментов методом POST /mcp с Content-Type: application/json в формате JSON-RPC 2.0. Успешный ответ ДОЛЖЕН иметь форму `{"jsonrpc":"2.0","id":...,"result":{"content":[{"type":"text","text":"<JSON-строка результата>"}]}}`, где содержимое инструмента сериализуется JSON-строкой внутри `content[0].text`.

#### Scenario: Успешный вызов инструмента

- **WHEN** агент отправляет валидный JSON-RPC 2.0 запрос tools/call с корректными аргументами
- **THEN** сервер отвечает HTTP 200 и телом `result.content[0]` с `type: "text"` и JSON-строкой результата в поле `text`

### Requirement: Обязательные заголовки протокола MCP 2026-07-28

Каждый POST /mcp ДОЛЖЕН содержать заголовок `MCP-Protocol-Version: 2026-07-28`. Заголовок `Mcp-Method` ДОЛЖЕН присутствовать и ДОЛЖЕН совпадать с фактическим JSON-RPC method тела запроса. Заголовок `Mcp-Name` ДОЛЖЕН присутствовать только для вызовов инструментов (method = tools/call или direct-mode имя инструмента) и ДОЛЖЕН совпадать с именем инструмента; для server/discover, tools/list и прочих методов без имени инструмента заголовок Mcp-Name НЕ ОБЯЗАТЕЛЕН. Когда тело несёт `_meta["io.modelcontextprotocol/protocolVersion"]`, сервер ОБЯЗАН сверить его с заголовком MCP-Protocol-Version; при отсутствии заголовка значение берётся из `_meta`, при противоречии — запрос отклоняется. Запрос с неподдерживаемой версией ДОЛЖЕН отклоняться ошибкой, `data` которой содержит `supportedVersions` — список поддерживаемых версий (семантика UnsupportedProtocolVersionError). Протокол ДОЛЖЕН быть stateless: без initialize/initialized и без Mcp-Session-Id; каждый запрос самодостаточен.

#### Scenario: Discover без Mcp-Name проходит

- **WHEN** POST /mcp содержит `{"jsonrpc":"2.0","id":0,"method":"server/discover"}` с заголовками MCP-Protocol-Version и Mcp-Method: server/discover, но без Mcp-Name
- **THEN** сервер обрабатывает запрос как анонимный discovery-вызов и возвращает структурный результат discovery

#### Scenario: Несовпадение Mcp-Method с методом тела

- **WHEN** тело содержит method "tools/list", а заголовок Mcp-Method равен "tools/call"
- **THEN** сервер возвращает ошибку -32600 Invalid Request

#### Scenario: Противоречие версии заголовка и _meta

- **WHEN** заголовок MCP-Protocol-Version равен "2025-11-25", а `_meta["io.modelcontextprotocol/protocolVersion"]` в теле равен "2026-07-28"
- **THEN** сервер возвращает ошибку -32600, чья data перечисляет supportedVersions сервера

#### Scenario: Версия только из _meta

- **WHEN** POST /mcp отправлен без заголовка MCP-Protocol-Version, но с `_meta["io.modelcontextprotocol/protocolVersion"]: "2026-07-28"`
- **THEN** сервер использует версию из _meta и обрабатывает запрос штатно

#### Scenario: Неподдерживаемая версия

- **WHEN** запрос содержит версию протокола, которую сервер не поддерживает
- **THEN** сервер возвращает ошибку с data.supportedVersions: ["2026-07-28"]

#### Scenario: Неизвестная версия протокола

- **WHEN** запрос содержит версию протокола (в заголовке и/или `_meta`), отличную от поддерживаемой
- **THEN** сервер возвращает ошибку -32600 Invalid Request с data.supportedVersions

#### Scenario: Отсутствуют обязательные заголовки

- **WHEN** POST /mcp с вызовом инструмента отправлен без одного из заголовков MCP-Protocol-Version, Mcp-Method, Mcp-Name
- **THEN** сервер возвращает ошибку -32600 Invalid Request

### Requirement: Согласование версий заголовок-мета

Сервер ДОЛЖЕН принимать версию протокола из заголовка MCP-Protocol-Version либо, при его отсутствии, из `_meta["io.modelcontextprotocol/protocolVersion"]` тела. При наличии обоих источников и их расхождении сервер ОБЯЗАН отклонить запрос ошибкой Invalid Request (-32600), включающей data.supportedVersions. Ответы server/discover ДОЛЖНЫ содержать полный список поддерживаемых версий для повторной попытки клиента с взаимно поддерживаемой версией.

#### Scenario: Повторная попытка после mismatch

- **WHEN** клиент получил ошибку с data.supportedVersions и повторил запрос с версией "2026-07-28"
- **THEN** сервер обрабатывает повторный запрос штатно

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

Метод tools/list ДОЛЖЕН возвращать структурный результат верхнего уровня с полем resultType "complete" (без обёртки content), содержащий JSON-схемы всех инструментов в массиве tools, а также ttlMs и cacheScope: "server", в детерминированном лексикографическом порядке по имени инструмента. Каждый инструмент в каталоге ОБЯЗАН иметь description и валидную inputSchema (JSON Schema draft 2020-12): inputSchema ДОЛЖЕН быть объектом с type "object"; каждое значение в properties ДОЛЖНО быть объектом-схемой с полем type из множества string|number|integer|boolean|array; массив required ДОЛЖЕН быть подмножеством имён properties. Значение ttlMs ДОЛЖНО быть конфигурируемым (по умолчанию 300000 мс). Поле decision в схеме cast_vote ДОЛЖНО быть enum всех теоретических вариантов (YES, NO, ABSTAIN); фактическая валидация выполняется по модели консенсуса организации.

#### Scenario: Структурный каталог с resultType complete

- **WHEN** клиент вызывает tools/list
- **THEN** result содержит resultType "complete", массив tools на верхнем уровне результата (без content-обёртки), ttlMs и cacheScope: "server"

#### Scenario: Детерминированный каталог

- **WHEN** агент дважды вызывает tools/list
- **THEN** оба ответа содержат одинаковый набор инструментов, отсортированный лексикографически по имени, с ttlMs и cacheScope: "server"

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

Каталог ДОЛЖЕН содержать инструмент `get_playbook` — анонимный (как register_agent, server/discover, tools/list), без аргументов, возвращающий структурированный текст онбординг-плейбука: порядок первого подключения (register_agent → атомарное сохранение пары agent_id+api_key в конфигурации MCP server харнесса → update_agent), проверку живости через get_agent в следующих сессиях, запрет повторной регистрации при -32001, discovery и вступление (OPEN мгновенно; CLOSED через PENDING с консенсусным одобрением), рабочий цикл предложений и контракт исходов (синхронный ответ cast_vote, плановый поллинг от expires_at, эффекты состояния, SSE как ускоритель), мульти-орг этикет. Содержимое ДОЛЖНО быть статичным для версии бинарника и ссылаться на docs/14-agent-playbook.md. Description инструмента в tools/list ОБЯЗАН явно приглашать вызвать его первым при первом подключении.

#### Scenario: Анонимный вызов плейбука

- **WHEN** новый агент вызывает get_playbook без Authorization
- **THEN** возвращается полный текст плейбука с шагами онбординга и контрактом исходов

#### Scenario: Плейбук обнаружим через каталог

- **WHEN** агент вызывает tools/list
- **THEN** в каталоге присутствует get_playbook с inputSchema типа object без обязательных аргументов и приглашающим description

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
