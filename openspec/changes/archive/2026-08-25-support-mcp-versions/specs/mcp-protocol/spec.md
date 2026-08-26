## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: Handshake initialize/initialized для стандартных клиентов

Метод initialize ДОЛЖЕН обрабатываться анонимно (без Authorization и `_meta`-токена) и возвращать стандартную форму результата: `{protocolVersion, capabilities, serverInfo}`, где protocolVersion равен версии, согласованной по правилам переговоров, capabilities содержит объект tools, serverInfo содержит name "voterpool" и версию бинарника. Согласование версии: если запрошенная клиентом версия входит в список поддерживаемых, сервер ОБЯЗАН вернуть её же (эхо); иначе — старшую поддерживаемую версию. Ответ initialize НЕ ДОЛЖЕН содержать Mcp-Session-Id: сервер остаётся stateless. Уведомления (JSON-RPC запросы без поля id, включая notifications/initialized) ДОЛЖНЫ отвечать HTTP 202 с пустым телом и НЕ ДОЛЖНЫ получать JSON-RPC ответ или ошибку.

#### Scenario: Полный handshake стандартного клиента

- **WHEN** клиент последовательно отправляет initialize с protocolVersion "2025-06-18", затем notification initialized, затем tools/list — все без кастомных заголовков
- **THEN** initialize отвечает result с protocolVersion "2025-06-18", capabilities.tools и serverInfo.name "voterpool"; notification получает HTTP 202 без тела; tools/list возвращает каталог инструментов

#### Scenario: Эхо поддержанной версии

- **WHEN** initialize приходит с params.protocolVersion "2026-07-28"
- **THEN** result.protocolVersion равен "2026-07-28"

#### Scenario: Fallback на неизвестной версии

- **WHEN** initialize приходит с params.protocolVersion, отсутствующей в supportedVersions
- **THEN** result.protocolVersion равен "2026-07-28"

#### Scenario: Анонимность initialize

- **WHEN** initialize отправлен без Authorization и без `_meta`-токена
- **THEN** handshake проходит без -32001

#### Scenario: Любое уведомление получает 202

- **WHEN** POST /mcp содержит JSON-RPC тело без поля id (например, notifications/cancelled)
- **THEN** сервер отвечает HTTP 202 с пустым телом

### Requirement: Методы HTTP эндпоинта /mcp

Эндпоинт /mcp ДОЛЖЕН принимать только POST. Запросы GET и DELETE к /mcp ДОЛЖНЫ отвечать HTTP 405 Method Not Allowed без разбора тела. SSE-канал доменных событий остаётся на отдельном эндпоинте GET /mcp/events.

#### Scenario: GET /mcp отвечает 405

- **WHEN** клиент запрашивает GET /mcp с Accept: text/event-stream
- **THEN** сервер отвечает HTTP 405 и клиентская сторона может продолжить работу в standalone-режиме

#### Scenario: DELETE /mcp отвечает 405

- **WHEN** клиент отправляет DELETE /mcp для завершения сессии
- **THEN** сервер отвечает HTTP 405; состояние сервера не меняется
