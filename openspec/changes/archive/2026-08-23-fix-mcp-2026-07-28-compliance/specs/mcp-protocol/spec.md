# Delta: mcp-protocol

## MODIFIED Requirements

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

Метод tools/list ДОЛЖЕН возвращать структурный результат верхнего уровня с полем resultType "complete" (без обёртки content), содержащий JSON-схемы всех инструментов в массиве tools, а также ttlMs и cacheScope: "server", в детерминированном лексикографическом порядке по имени инструмента. Каждый инструмент в каталоге ОБЯЗАН иметь description и валидную inputSchema (JSON Schema draft 2020-12). Значение ttlMs ДОЛЖНО быть конфигурируемым (по умолчанию 300000 мс). Поле decision в схеме cast_vote ДОЛЖНО быть enum всех теоретических вариантов (YES, NO, ABSTAIN); фактическая валидация выполняется по модели консенсуса организации.

#### Scenario: Структурный каталог с resultType complete

- **WHEN** клиент вызывает tools/list
- **THEN** result содержит resultType "complete", массив tools на верхнем уровне результата (без content-обёртки), ttlMs и cacheScope: "server"

#### Scenario: Детерминированный каталог

- **WHEN** агент дважды вызывает tools/list
- **THEN** оба ответа содержат одинаковый набор инструментов, отсортированный лексикографически по имени, с ttlMs и cacheScope: "server"

#### Scenario: Полные схемы инструментов

- **WHEN** агент вызывает tools/list
- **THEN** каждый элемент массива tools содержит name, description и inputSchema типа object

## ADDED Requirements

### Requirement: Согласование версий заголовок-мета

Сервер ДОЛЖЕН принимать версию протокола из заголовка MCP-Protocol-Version либо, при его отсутствии, из `_meta["io.modelcontextprotocol/protocolVersion"]` тела. При наличии обоих источников и их расхождении сервер ОБЯЗАН отклонить запрос ошибкой Invalid Request (-32600), включающей data.supportedVersions. Ответы server/discover ДОЛЖНЫ содержать полный список поддерживаемых версий для повторной попытки клиента с взаимно поддерживаемой версией.

#### Scenario: Повторная попытка после mismatch

- **WHEN** клиент получил ошибку с data.supportedVersions и повторил запрос с версией "2026-07-28"
- **THEN** сервер обрабатывает повторный запрос штатно
