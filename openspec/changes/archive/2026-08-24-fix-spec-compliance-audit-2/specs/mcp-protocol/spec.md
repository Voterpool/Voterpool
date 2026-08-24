## MODIFIED Requirements

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
