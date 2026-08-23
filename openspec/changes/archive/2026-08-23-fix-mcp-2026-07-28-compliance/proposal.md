# Fix MCP 2026-07-28 Compliance

## Why

Заявленная совместимость с MCP 2026-07-28 нарушена в четырёх местах, из-за чего стоковый клиент не может подключиться вообще: обязательная по спецификации проба `server/discover` возвращает текстовый блоб вместо структурного результата → клиент классифицирует сервер как legacy → fallback на initialize handshake → метод не поддерживается → -32601. Отклонения: (1) `server/discover` и `tools/list` завёрнуты в `content[0].text`, хотя спека требует прямой структурный `result`; (2) payload discover не соответствует официальной схеме (`protocolVersion` вместо массива `supportedVersions`, нет `resultType`/`ttlMs`/`cacheScope`, `serverInfo` вне `_meta.io.modelcontextprotocol/serverInfo`); (3) middleware требует `Mcp-Method: tools/call` и непустой `Mcp-Name` для любого POST — честный клиент с `Mcp-Method: server/discover` отбрасывается до диспетчера; (4) версия заголовка не сверяется с `_meta["io.modelcontextprotocol/protocolVersion"]` тела, а mismatch отдаёт голый -32600 вместо ошибки со списком поддерживаемых версий. Девиантные форматы кодифицированы в docs/05 §1.0 и в спеке mcp-protocol — чинить надо и документы.

## What Changes

- **BREAKING**: `server/discover` и `tools/list` возвращают структурный результат напрямую (`result.resultType: "complete"`, поля верхнего уровня), без обёртки `content[0].text`. Клиенты, парсящие старую обёртку этих двух методов, должны обновиться. Результаты `tools/call` остаются text-content (легальны; отсутствие resultType трактуется клиентами как complete).
- Payload discover приводится к официальной схеме: `resultType`, `supportedVersions: ["2026-07-28"]`, `capabilities.tools`, `_meta.io.modelcontextprotocol/serverInfo`, опционально `instructions`, `ttlMs`, `cacheScope`; расширение `io.voterpool/domain-events` сохраняется.
- Middleware `/mcp`: `Mcp-Method` сверяется с фактическим JSON-RPC method тела (а не жёстко «tools/call»); `Mcp-Name` обязателен только для tools/call и direct-mode вызовов инструментов; при наличии `_meta.protocolVersion` в теле — кросс-проверка с заголовком `MCP-Protocol-Version`; несоответствие версии → ошибка с `data.supportedVersions` (семантика UnsupportedProtocolVersionError).
- Документация: правка docs/05-mcp-contracts.md §1.0 (примеры discover/tools/list — официальные форматы; правила заголовков), curl-примеры в README.md / README.ru.md.
- Спецификация: дельта `mcp-protocol` — переработка требований «Обязательные заголовки», «Анонимные методы» (форма ответа discover), «Кэшируемый каталог tools/list» (структурный результат + resultType), новое требование согласования версии заголовок↔_meta.
- Тесты:
  - новый e2e-профиль «стоковый клиент»: discover без Mcp-Name, версия только в `_meta`, разбор структурного каталога;
  - тест middleware: Mcp-Method = фактический метод проходит; tools/call без Mcp-Name → -32600; mismatch заголовок↔_meta версии → ошибка с supportedVersions;
  - обновление **всех** существующих e2e/интеграционных тестов, читающих старую обёртку discover/tools/list, на структурные ответы с `resultType: "complete"` (приведение всего тестового набора к формату complete);
  - регресс: режим B (direct method call) и режим A дают идентичные результаты.

## Capabilities

### New Capabilities

(нет)

### Modified Capabilities

- `mcp-protocol`: изменяются требования к форме результатов server/discover и tools/list (структурный result с resultType), к обязательности заголовков Mcp-Method/Mcp-Name по типу метода и к согласованию версии протокола между заголовком и `_meta`.

## Impact

- `src/mcp/McpHandler.cpp` — ветки discover/tools/list, форма результата.
- `src/server/AuthMiddleware.cpp` — проверка заголовков POST /mcp.
- `docs/05-mcp-contracts.md`, `README.md`, `README.ru.md` — примеры и правила.
- `openspec/specs/mcp-protocol/spec.md` — через дельту изменения.
- `tests/e2e/test_mcp_flow.cpp`, `tests/common/HttpUtil.*`, прочие тесты, использующие старую обёртку discovery-методов.
- Обоснование заголовков (роутинг шлюзами без парсинга JSON) сохраняется: спека сама требует Mcp-Method/Mcp-Name; корректируется лишь строгость их проверки.
