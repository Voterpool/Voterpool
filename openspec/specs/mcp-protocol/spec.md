# MCP-протокол (mcp-protocol)

## Purpose

Транспортное и протокольное ядро MCP 2026-07-28: единая точка приёма JSON-RPC 2.0 запросов агентов (POST /mcp), обязательные заголовки протокола, диспетчеризация инструментов, анонимные методы (server/discover, tools/list, register_agent) и единый контракт ошибок с HTTP-маппингом (docs/05, docs/07).

Область действия: редакция **On-Premises (Self-Hosted)**. Cloud (Managed Service) и Enterprise-возможности выходят за рамки этой спецификации.

## Requirements

### Requirement: Единый эндпоинт POST /mcp

Система ДОЛЖНА принимать все вызовы инструментов методом POST /mcp с Content-Type: application/json в формате JSON-RPC 2.0. Успешный ответ ДОЛЖЕН иметь форму `{"jsonrpc":"2.0","id":...,"result":{"content":[{"type":"text","text":"<JSON-строка результата>"}]}}`, где содержимое инструмента сериализуется JSON-строкой внутри `content[0].text`.

#### Scenario: Успешный вызов инструмента

- **WHEN** агент отправляет валидный JSON-RPC 2.0 запрос tools/call с корректными аргументами
- **THEN** сервер отвечает HTTP 200 и телом `result.content[0]` с `type: "text"` и JSON-строкой результата в поле `text`

### Requirement: Обязательные заголовки протокола MCP 2026-07-28

Каждый POST /mcp ДОЛЖЕН содержать заголовки `MCP-Protocol-Version: 2026-07-28`, `Mcp-Method: tools/call` и `Mcp-Name: <tool_name>`. Запрос с неизвестной версией в MCP-Protocol-Version ДОЛЖЕН отклоняться с ошибкой -32600 Invalid Request. Протокол ДОЛЖЕН быть stateless: без initialize/initialized и без Mcp-Session-Id; каждый запрос самодостаточен.

#### Scenario: Неизвестная версия протокола

- **WHEN** запрос содержит заголовок MCP-Protocol-Version со значением, отличным от поддерживаемого
- **THEN** сервер возвращает ошибку -32600 Invalid Request

#### Scenario: Отсутствуют обязательные заголовки

- **WHEN** POST /mcp отправлен без одного из заголовков MCP-Protocol-Version, Mcp-Method, Mcp-Name
- **THEN** сервер возвращает ошибку -32600 Invalid Request

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

Методы server/discover, tools/list и register_agent ДОЛЖНЫ быть доступны без заголовка Authorization. Все остальные инструменты при отсутствии валидного токена ДОЛЖНЫ возвращать -32001 Unauthorized.

#### Scenario: Анонимный вызов server/discover

- **WHEN** клиент вызывает `{"jsonrpc":"2.0","id":0,"method":"server/discover"}` без Authorization
- **THEN** сервер возвращает объект с protocolVersion "2026-07-28", capabilities.tools, extensions `io.voterpool/domain-events` с эндпоинтом /mcp/events и serverInfo {name: "voterpool"}

### Requirement: Кэшируемый каталог tools/list

Метод tools/list ДОЛЖЕН возвращать JSON-схемы всех инструментов с полями ttlMs и cacheScope: "server", в детерминированном лексикографическом порядке по имени инструмента. Каждый инструмент в каталоге ОБЯЗАН иметь description и валидную inputSchema (JSON Schema draft 2020-12). Значение ttlMs ДОЛЖНО быть конфигурируемым (по умолчанию 300000 мс). Поле decision в схеме cast_vote ДОЛЖНО быть enum всех теоретических вариантов (YES, NO, ABSTAIN); фактическая валидация выполняется по модели консенсуса организации.

#### Scenario: Детерминированный каталог

- **WHEN** агент дважды вызывает tools/list
- **THEN** оба ответа содержат одинаковый набор инструментов, отсортированный лексикографически по имени, с ttlMs и cacheScope: "server"

#### Scenario: Полные схемы инструментов

- **WHEN** агент вызывает tools/list
- **THEN** каждый элемент массива tools содержит name, description и inputSchema типа object

### Requirement: Метаданные клиента _meta

Поле `_meta.io.modelcontextprotocol/clientInfo` (имя/версия клиента) ДОЛЖНО извлекаться для логов и метрик и НЕ ДОЛЖНО влиять на авторизацию.

#### Scenario: clientInfo не влияет на авторизацию

- **WHEN** запрос содержит _meta.clientInfo или не содержит его
- **THEN** авторизация выполняется исключительно по токену Authorization, и вызов обрабатывается одинаково

### Requirement: Контракт ошибок JSON-RPC

Любая ошибка валидации или бизнес-логики ДОЛЖНА возвращаться с HTTP-статусом 200 в теле JSON-RPC 2.0 с полем error. Исключения: Parse error (-32700) → HTTP 400; деградация хранилища (-32050) → HTTP 503. Поле data ОБЯЗАТЕЛЬНО для кастомных кодов (-32000..-32099) и опционально для стандартных. Стандартные коды: -32700 Parse error (id = null), -32600 Invalid Request, -32601 Method not found, -32602 Invalid params, -32603 Internal error. Кастомные коды: -32001 Unauthorized, -32002 Forbidden, -32003 Conflict, -32004 Not Found, -32005 Business Rule Violation.

#### Scenario: Ошибка бизнес-логики с HTTP 200

- **WHEN** агент голосует по несуществующему proposal_id
- **THEN** сервер отвечает HTTP 200 с error.code = -32004 и заполненным error.data

#### Scenario: Невалидный JSON

- **WHEN** тело POST /mcp не является корректным JSON
- **THEN** сервер отвечает HTTP 400 с error.code = -32700 и id = null

#### Scenario: Неверные типы аргументов

- **WHEN** обязательный аргумент отсутствует или передан неверного типа (строка вместо числа, невалидный UUID)
- **THEN** сервер возвращает error.code = -32602 Invalid params
