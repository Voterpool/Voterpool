## REMOVED Requirements

### Requirement: Методы HTTP эндпоинта /mcp

**Reason**: Требование переработано после полевых данных совместимости: клиенты ревизий MCP 2025-* открывают standalone GET-поток после handshake и терпят фатальный отказ на 405 (подтверждённый кейс opencode — «sse error non-200 status code (405)»). Сервер начинает поддержку GET как молчащего keepalive SSE-потока (многоревизионная совместимость, разрешённая спецификацией), поэтому правило «GET → 405» устарело.

**Migration**: См. ADDED требование «Транспортные методы эндпоинта /mcp»: POST и DELETE сохраняют прежнее поведение, GET открывает поток.

## ADDED Requirements

### Requirement: Транспортные методы эндпоинта /mcp

Эндпоинт /mcp ДОЛЖЕН принимать POST для JSON-RPC и GET для standalone SSE-потока. Запрос GET /mcp ДОЛЖЕН отвечать HTTP 200 с Content-Type: text/event-stream и держать соединение открытым, передавая только heartbeat-комментарии (строки вида `: ping`) с интервалом sse.heartbeat_interval_sec; сервер НЕ ДОЛЖЕН посылать в этом потоке JSON-RPC сообщений, событий endpoint или session-идентификаторов. Поток ДОЛЖЕН быть доступен анонимно; при наличии заголовка Authorization агент ДОЛЖЕН резолвиться для логов, а невалидный токен НЕ ДОЛЖЕН закрывать поток. При закрытии соединения клиентом сервер ДОЛЖЕН освобождать ресурсы потока. Запросы DELETE к /mcp ДОЛЖНЫ отвечать HTTP 405 Method Not Allowed. SSE-канал доменных событий остаётся на отдельном эндпоинте GET /mcp/events.

#### Scenario: GET /mcp открывает keepalive-поток

- **WHEN** клиент запрашивает GET /mcp с Accept: text/event-stream
- **THEN** сервер отвечает HTTP 200, Content-Type: text/event-stream, и соединение остаётся открытым

#### Scenario: Heartbeat поддерживает поток живым

- **WHEN** GET-соединение /mcp открыто дольше sse.heartbeat_interval_sec
- **THEN** клиент получает хотя бы один комментарий-кадр (строка, начинающаяся с ":"), JSON-RPC сообщений в потоке нет

#### Scenario: Старый клиент не получает отказа на GET

- **WHEN** клиент ревизии 2025-* после успешного initialize открывает GET /mcp
- **THEN** запрос завершается статусом 200 и транспорт клиента не получает ошибки протокола

#### Scenario: Невалидный токен не рвёт поток

- **WHEN** GET /mcp отправлен с заголовком Authorization: Bearer <несуществующий токен>
- **THEN** сервер отвечает 200 и открывает поток как для анонимного клиента

#### Scenario: DELETE /mcp отвечает 405

- **WHEN** клиент отправляет DELETE /mcp для завершения сессии
- **THEN** сервер отвечает HTTP 405; состояние сервера не меняется

## MODIFIED Requirements

### Requirement: Handshake initialize/initialized для стандартных клиентов

Метод initialize ДОЛЖЕН обрабатываться анонимно (без Authorization и `_meta`-токена) и возвращать стандартную форму результата: `{protocolVersion, capabilities, serverInfo}`, где protocolVersion равен версии, согласованной по правилам переговоров, capabilities содержит объект tools, serverInfo содержит name "voterpool" и версию бинарника. Согласование версии: если запрошенная клиентом версия входит в список поддерживаемых, сервер ОБЯЗАН вернуть её же (эхо); иначе — ближайшую МЛАДШУЮ поддерживаемую версию: клиент понимает версии не новее своей запрошенной, а ответ незнакомой версией вызывает обязательный по спецификации дисконнект клиента; если запрошенная старше всех поддерживаемых — самую старую поддерживаемую. Ответ initialize НЕ ДОЛЖЕН содержать Mcp-Session-Id: сервер остаётся stateless. Уведомления (JSON-RPC запросы без поля id, включая notifications/initialized) ДОЛЖНЫ отвечать HTTP 202 с пустым телем и НЕ ДОЛЖНЫ получать JSON-RPC ответ или ошибку.

#### Scenario: Полный handshake стандартного клиента

- **WHEN** клиент последовательно отправляет initialize с protocolVersion "2025-06-18", затем notification initialized, затем tools/list — все без кастомных заголовков
- **THEN** initialize отвечает result с protocolVersion "2025-06-18", capabilities.tools и serverInfo.name "voterpool"; notification получает HTTP 202 без тела; tools/list возвращает каталог инструментов

#### Scenario: Эхо поддержанной версии

- **WHEN** initialize приходит с params.protocolVersion "2026-07-28"
- **THEN** result.protocolVersion равен "2026-07-28"

#### Scenario: Эхо промежуточной поддержанной версии

- **WHEN** initialize приходит с params.protocolVersion "2025-11-25" (реальный профиль opencode)
- **THEN** result.protocolVersion равен "2025-11-25"

#### Scenario: Fallback на неизвестной версии

- **WHEN** initialize приходит с params.protocolVersion, отсутствующей в supportedVersions (например "1999-01-01")
- **THEN** result.protocolVersion равен ближайшей младшей поддерживаемой ("2025-03-26")

#### Scenario: Запрос между поддержанными версиями

- **WHEN** initialize приходит с params.protocolVersion "2025-09-01"
- **THEN** result.protocolVersion равен ближайшей младшей поддерживаемой ("2025-06-18")

#### Scenario: Анонимность initialize

- **WHEN** initialize отправлен без Authorization и без `_meta`-токена
- **THEN** handshake проходит без -32001

#### Scenario: Любое уведомление получает 202

- **WHEN** POST /mcp содержит JSON-RPC тело без поля id (например, notifications/cancelled)
- **THEN** сервер отвечает HTTP 202 с пустым телом
