# Tasks: поддержка разных версий MCP

## 1. Конфигурация списка версий

- [x] 1.1 В `include/core/Config.h`/`src/core/Config.cpp` добавить `McpConfig::supported_versions` (vector<string>, парсинг YAML-массива и CSV-env), дефолт `["2026-07-28","2025-06-18","2025-03-26"]`, env-override `VOTERPOOL_MCP_SUPPORTED_VERSIONS`; валидация: непустой список непустых строк, содержит `protocol_version`, иначе ConfigError (exit 1). Проверка: unit-тест в `tests/unit` на дефолт, CSV-парсинг и оба отказа валидации
- [x] 1.2 Добавить `mcp.supported_versions` в `config/default.yaml` и секцию mcp в `docs/08-app-config.md` (ключ, дефолт, env-переменная, правило «protocol_version входит в список»). Проверка: сервер стартует с дефолтным конфигом; docs описывают ключ

## 2. AuthMiddleware: версия и заголовки становятся необязательными

- [x] 2.1 Расширить `probeBody` извлечением `params.protocolVersion` и имени инструмента (`params.name`); определить эффективную версию запроса по приоритету заголовок → params.protocolVersion → _meta; отклонять -32600 c data.supportedVersions только при реально заявленной версии вне `mcp.supported_versions` или расхождении источников. Проверить сохранение контракта -32700: тело без method проходит дальше без предварительных отказов
- [x] 2.2 Перевести проверки Mcp-Method/Mcp-Name в validate-if-present: при наличии — сверка с телом (-32600 при расхождении), при отсутствии — значения для mwLog из probeBody. Проверка: e2e tools/call register_agent без единого MCP-заголовка возвращает result; расхождение Mcp-Name↔params.name → -32600

## 3. Handshake в McpHandler

- [x] 3.1 Добавить ветку `initialize`: анонимный ответ `{protocolVersion, capabilities:{tools:{listChanged:false}}, serverInfo:{name:"voterpool",version}}`, protocolVersion = эхо запрошенной из supported_versions иначе первый элемент списка; без Mcp-Session-Id; метрики/логи method=initialize. Проверка: e2e initialize без заголовков → result.protocolVersion равен запрошенной
- [x] 3.2 Ответ HTTP 202 с пустым телом на все JSON-RPC уведомления (нет поля id) сразу после парсинга, до диспетчеризации. Проверка: e2e notifications/initialized → 202 пустое тело; notifications/cancelled → 202
- [x] 3.3 Зарегистрировать GET/DELETE обработчики `/mcp` в `VoterpoolApp.cpp`: 405 + `Allow: POST`. Проверка: e2e GET /mcp → 405, DELETE /mcp → 405

## 4. E2E-контракты и регресс

- [x] 4.1 В `tests/e2e/test_protocol.cpp` сценарий полного handshake стандартного клиента: initialize("2025-06-18") → notification(202) → tools/list → tools/call register_agent — все POST без кастомных заголовков. Проверка: тест зелёный
- [x] 4.2 Матрица версий: эхо "2026-07-28"; fallback initialize с "1999-01-01" → "2026-07-28"; не-initialize запрос с неизвестной версией → -32600 + supportedVersions; расхождение заголовок↔params.protocolVersion → -32600; отсутствие версии обрабатывается. Проверка: тесты зелёные
- [x] 4.3 Регресс legacy-диалекта в `test_transport.cpp`: режим A и B с полным набором заголовков, `_meta`-auth, discover/tools/list с заголовками — результаты идентичны прежним. Проверка: существующие e2e/integration suites зелёные (`ctest -preset release`)
- [x] 4.4 Ошибочный контракт: битое тело с любыми заголовками → -32700/400 (не -32600). Проверка: сценарий в `test_errors_http.cpp` зелёный

## 5. Документация

- [x] 5.1 `docs/05-mcp-contracts.md`: переписать §1.0 — заголовки опциональны, переговоры версии (источники, supported_versions, эхо/fallback), handshake initialize/notifications, 405 GET/DELETE, stateless-приоритет; обновить примеры curl (стандартный клиент без заголовков). Проверка: примеры раздела воспроизводимы curl'ом против локального сервера
- [x] 5.2 README.md/README.ru.md: quick-start дополнить конфигом стандартного харнесса (opencode/Claude/Cursor mcp.json) без прокси; убрать импликацию обязательности заголовков. Проверка: приведённый JSON подключается opencode к VPS без ошибок в логах
- [x] 5.3 `docs/14-agent-playbook.md` и `docs/07-errors.md`: раздел «подключение через harness» (handshake теперь есть), уточнить условия -32600 (расхождение источников/заголовков, неизвестная версия). Проверка: текст согласован с новой спекой, ссылки на docs/05 корректны

## 6. Финальная верификация

- [x] 6.1 Прогнать полный набор: `cmake --preset release && cmake --build --preset release && ctest -preset release` — unit+integration+e2e зелёные. Проверка: ctest 100% pass
- [x] 6.2 Ручная проверка воспроизведённого кейса: поднять сервис с дефолтным конфигом, подключить opencode (type http, url /mcp) — initialize в логах outcome=ok, инструменты доступны. Проверка: в логах нет -32600 на initialize
