# Tasks: Fix MCP 2026-07-28 Compliance

## 1. Структурные ответы discovery

- [x] 1.1 McpHandler: добавить `respondStructured(id, Json::Value)` (payload прямо в result, без content) и переключить ветки server/discover и tools/list. Проверка: ручной curl обоих методов показывает поля на верхнем уровне result.
- [x] 1.2 Привести discoverResponse к официальной схеме: resultType "complete", supportedVersions: ["2026-07-28"], _meta["io.modelcontextprotocol/serverInfo"] {name, version}, ttlMs, cacheScope "public"; extensions io.voterpool/domain-events сохранены. Проверка: curl server/discover соответствует таблице D1 design.
- [x] 1.3 toolsListResponse: добавить resultType "complete" (tools/ttlMs/cacheScope уже корректны). Проверка: curl tools/list содержит resultType и массив tools на верхнем уровне.

## 2. Middleware

- [x] 2.1 AuthMiddleware: частичный simdjson-парсинг тела (method, params._meta["io.modelcontextprotocol/protocolVersion"]); правило Mcp-Method = method тела; Mcp-Name обязателен только для tools/call и direct-mode имён каталога; для discovery — опционален. Проверка: unit/интеграционные тесты middleware зелёные.
- [x] 2.2 Кросс-проверка версии: заголовок приоритетен; расхождение с _meta → -32600 c data {reason, supportedVersions}; отсутствие заголовка при валидном _meta — допустимо; отсутствие обоих → -32600. Проверка: тесты из 2.4 покрывают все четыре ветки.
- [x] 2.3 Метрики ошибок сохраняют лейблы (voterpool_rpc_errors_total code=-32600) на новых путях отказа. Проверка: /metrics инкрементируется после отклонённого запроса.
- [x] 2.4 Новые тесты middleware: mismatch заголовок↔_meta; версия только в _meta; discover без Mcp-Name проходит; tools/call без Mcp-Name → -32600; Mcp-Method ≠ методу тела → -32600. Проверка: тесты зелёные.

## 3. Тесты e2e

- [x] 3.1 HttpUtil: хелперы mcpHeaders(method, toolName) и профиль «стоковый клиент» (discovery без Mcp-Name, версия только в _meta). Проверка: сборка тестов.
- [x] 3.2 Переписать test_protocol.cpp ServerDiscoverAnonymous и ToolsListEnvelopeSortedAndComplete на структурные ответы (resultType complete, supportedVersions, _meta.serverInfo, отсутствие content; сортировка каталога сохраняется). Проверка: тесты зелёные против нового сервера.
- [x] 3.3 Обновить DirectDeprecatedModeEquivalentToToolsCall и остальные e2e, где Mcp-Method жёстко "tools/call" для не-tools/call методов, включая test_onboarding_http.cpp discovery-обращения. Проверка: grep по тестам не находит старой обёртки `result.content[0].text` у discovery-методов; весь набор e2e приведён к формату complete.
- [x] 3.4 Новый e2e «стоковый клиент»: probe server/discover → разбор структурного каталога → register_agent → рабочий цикл одной организации. Проверка: тест зелёный.
- [x] 3.5 Регресс эквивалентности режимов A/B и анонимности четырёх методов (discover/list/register_agent/get_playbook). Проверка: тесты зелёные.

## 4. Документация

- [x] 4.1 docs/05-mcp-contracts.md §1.0: примеры discover/tools/list — официальные форматы; правила заголовков (Mcp-Method = методу тела, Mcp-Name по типу метода); раздел о согласовании версий. Проверка: примеры дословно совпадают с ответами живого сервера (curl-сверка).
- [x] 4.2 README.md / README.ru.md: обновить curl-примеры заголовков. Проверка: копипаста примеров из README работает против локального сервера.

## 5. Финальная верификация

- [x] 5.1 Полный прогон unit+integration+e2e; сверка docs-примеров с живым сервером. Проверка: ctest полностью зелёный.
