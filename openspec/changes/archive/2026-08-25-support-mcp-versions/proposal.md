# Поддержка разных версий MCP: совместимость со стандартными клиентами

## Why

Сервер отклоняет любой стандартный MCP-клиент (opencode, Claude, Cursor) до диспетчеризации: middleware требует заголовки `Mcp-Method`/`Mcp-Name`, которых нет в реальных реализациях протокола, и жёсткое равенство версии с pinned `2026-07-28`, тогда как клиентские SDK присылают свою версию для переговоров. Реальный владелец VPS, подключивший opencode, получает `-32600 Invalid Request` на каждом `initialize` (логи req-11620-*). Даже пройдя middleware, `initialize` получил бы -32601 — хендлера не существует. Обещание README «If your agent speaks MCP, it already speaks Voterpool» сейчас не выполняется ни для одного off-the-shelf харнесса.

## What Changes

- Добавить полноценный **handshake** для клиентов, которые его требуют: обработка `initialize` (стандартная форма ответа `{protocolVersion, capabilities, serverInfo}`, согласование версии эхом запрошенной) и `notifications/initialized` (HTTP 202, пустое тело).
- Перевести заголовки `Mcp-Method`/`Mcp-Name` из обязательных в **validate-if-present**: отсутствие не ошибка; при наличии — обязаны совпадать с телом (иначе -32600). Маршрутизация/метрики по заголовкам сохраняются; при отсутствии значения выводятся из тела.
- Заменить строгое равенство версии на **переговоры по списку поддерживаемых**: новый конфиг `mcp.supported_versions` (по умолчанию `["2026-07-28", "2025-06-18", "2025-03-26"]`), версия запроса берётся из заголовка либо из тела (`params.protocolVersion` у initialize, `_meta`-расширение); неизвестная версия → -32600 с `data.supportedVersions`.
- Сохранить **stateless-ядро как приоритетное**: сервер не выдаёт `Mcp-Session-Id`; каждый запрос самодостаточен. Handshake — совместимый вход, а не переход на сессии.
- Явно отвечать **405 Method Not Allowed** на `GET /mcp` и `DELETE /mcp` (клиенты корректно переходят в standalone-режим); `/mcp/events` остаётся проприетарным SSE-каналом.
- Режим B (прямой вызов инструмента), discovery, `_meta`-авторизация и все существующие инструменты — без изменений.
- Документация: переписать раздел протокола в docs/05, добавить инструкции подключения стандартных харнессов (README, docs/14), актуализировать docs/07 и docs/08.
- Спеки: изменить требования capability `mcp-protocol` (заголовки, переговоры версии, handshake, HTTP-методы эндпоинта) и `configuration` (новый ключ mcp).

**BREAKING** (для строгих интеграций): POST /mcp без `MCP-Protocol-Version` больше не отклоняется; отсутствие `Mcp-Method`/`Mcp-Name` больше не ошибка. Существующие клиенты диалекта (заголовки присутствуют) продолжают работать без изменений.

## Capabilities

### New Capabilities

_(нет — изменения укладываются в существующие capabilities)_

### Modified Capabilities

- `mcp-protocol`: требование «Обязательные заголовки» становится «Опциональные заголовки с валидацией при наличии»; новые требования — handshake (initialize + notifications/initialized, анонимный), переговоры версии через supported_versions (эхо запрошенной), 405 на GET/DELETE /mcp; уточнение контракта ошибок (версия может приходить из params.protocolVersion).
- `configuration`: секция mcp дополняется ключом `supported_versions` (список, непустой, обязан содержать protocol_version), переопределение `VOTERPOOL_MCP_SUPPORTED_VERSIONS`, валидация при старте.

## Impact

- Код: `src/server/AuthMiddleware.cpp` (гейты версии/заголовков), `src/mcp/McpHandler.cpp` (+ ветки initialize/notifications, 405-обработка GET/DELETE), `include/mcp/McpHandler.h` (константы версий), `src/core/Config.cpp` + `include/core/Config.h` (+supported_versions), регистрация маршрутов в `src/server/VoterpoolApp.cpp`.
- Конфиг: `config/default.yaml` (+mcp.supported_versions), docs/08-app-config.md.
- Тесты: `tests/e2e/test_protocol.cpp`, `test_transport.cpp`, `test_errors_http.cpp` (handshake-матрица, регресс legacy-диалекта), unit-тесты Config.
- Не затрагивается: инструменты каталога, консенсус, хранилище, SSE-хаб, авторизация.
