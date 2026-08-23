# Design: Fix MCP 2026-07-28 Compliance

## Context

Четыре отклонения от официальной спецификации 2026-07-28 (разбор — proposal.md): обёртка discover/tools/list в `content[0].text` (McpHandler.cpp:275, 283), девиантная схема discover-полезной нагрузки (McpHandler.cpp:169), избыточно строгий middleware (AuthMiddleware.cpp:55-66), отсутствие кросс-проверки версии заголовок↔`_meta`. Девиантные форматы кодифицированы в docs/05 §1.0 и спеке mcp-protocol — дельта спецификации уже написана в этом изменении.

Тестовый ландшафт: e2e-тесты строят запросы вручную (test_protocol.cpp:21-41); discovery-ответы распаковываются через `result.content[0].text` (test_protocol.cpp:46-52) в тестах `ServerDiscoverAnonymous`, `ToolsListEnvelopeSortedAndComplete`; test_onboarding_http.cpp тоже обращается к discovery. Интеграционные тесты вызывают `dispatchToolForTests` напрямую, минуя HTTP-слой, и не затрагиваются.

## Goals / Non-Goals

**Goals**
- Структурные результаты discover/tools/list по официальной схеме.
- Middleware, принимающий любого корректного клиента 2026-07-28 (включая пробу discovery без Mcp-Name).
- Согласование версий заголовок↔`_meta` с UnsupportedProtocolVersionError-семантикой.
- Синхронизация docs/05, README и тестов с новым контрактом.

**Non-Goals**
- `structuredContent` в результатах tools/call (легально без него; отдельное будущее улучшение).
- Поддержка нескольких версий протокола (сервер по-прежнему говорит только 2026-07-28; меняется форма отказа).
- MRTR/resultType: input_required (зарезервировано, не используется).
- Изменение режима B (direct-mode) и _meta-канала авторизации.

## Decisions

### D1: Новый путь ответа для discovery-методов

В McpHandler добавляется `respondStructured(id, const Json::Value&)`, ставящий payload прямо в `result`, минуя `resultBody`. Ветки `server/discover`/`tools/list` переключаются на него; `tools/call` и direct-mode сохраняют text-content обёртку (легальную и обратно совместимую).

Схема discover (официальная):

| Было (код + docs/05) | Стало |
|---|---|
| `protocolVersion: "…"` | `supportedVersions: ["2026-07-28"]` |
| — | `resultType: "complete"` |
| `serverInfo: {name, version}` | `_meta["io.modelcontextprotocol/serverInfo"]` |
| `extensions.io.voterpool/domain-events` | сохраняется без изменений (расширение) |
| — | `ttlMs`, `cacheScope: "public"` |

tools/list: добавляется `resultType: "complete"`; `tools`/`ttlMs`/`cacheScope` уже корректны, переезжают на верхний уровень result (сейчас — внутрь текстовой строки).

### D2: Валидация протокола остаётся в middleware с лёгким частичным парсингом тела

Проверка заголовков должна предшествовать авторизации (приоритет -32600 над -32001 сохраняется), поэтому из handler'а в middleware не переносится. Для кросс-проверки версии middleware извлекает из тела только `method` и `params._meta["io.modelcontextprotocol/protocolVersion"]` через simdjson (уже в зависимостях). Цена — второй парсинг тела в handler'е; на текущих объёмах некритично, кэширование распарсенного корня в request-attributes оставлено как будущая оптимизация.

Правила middleware:
- `Mcp-Method` обязателен и должен совпадать с `method` тела;
- `Mcp-Name` обязателен только если method — tools/call или имя инструмента из каталога (direct-mode); для server/discover и tools/list — не требуется;
- версия: заголовок приоритетен; при наличии обоих источников и расхождении → -32600 c `data.supportedVersions`; при отсутствии заголовка берётся `_meta`; при отсутствии обоих → -32600 (как сейчас).

Альтернатива «валифицировать всё в handler'е, middleware оставить health/auth» отклонена: ломает приоритет кодов ошибок и размазывает протокольный контракт по двум слоям.

### D3: Форма ошибки версии

Единый код -32600 Invalid Request с `data: {reason, supportedVersions: ["2026-07-28"]}` — соответствует дельте спеки; отдельного кода протокол не определяет, а клиенты 2026-07-28 классифицируют отказ по наличию `supportedVersions` (probe-логика backward compatibility).

### D4: Тесты

- `tests/common/HttpUtil.h` дополняется хелперами `mcpHeaders(method, toolName)` и профилем «стоковый клиент» (без Mcp-Name для discovery, версия только в `_meta`).
- `test_protocol.cpp`: `ServerDiscoverAnonymous` и `ToolsListEnvelopeSortedAndComplete` переписываются на структурные ответы (`resultType: "complete"`, `supportedVersions`, `_meta...serverInfo`, отсутствие `content`); `DirectDeprecatedModeEquivalentToToolsCall` обновляет `Mcp-Method` на фактический метод; добавляются сценарии middleware из дельты спеки.
- Интеграционные тесты (dispatchToolForTests) не меняются — слой диспетчеризации не тронут.

## Risks / Trade-offs

- [BREAKING для кастомных клиентов, парсящих старую обёртку discovery] → заявлено в proposal; README/docs обновляются в том же изменении; tools/call не затронут.
- [Двойной парсинг тела (middleware + handler)] → simdjson ondemand ~микросекунды на типичных телах; оптимизация отложена.
- [Расхождение docs/05 и спеки при частичном мерже] → docs-правки идут в тех же tasks, что и код.

## Migration Plan

Единый релиз: код + docs + README. Операторы с кастомными клиентами обновляют клиенты до структурного чтения discovery. Откат — revert (формат возвращается к прежнему без миграций данных).

## Open Questions

Нет.
