# Tasks: GET /mcp keepalive SSE-поток

## 1. Хендлер GET /mcp

- [x] 1.1 В `VoterpoolApp.cpp` заменить GET-обработчик `/mcp` (сейчас methodNotAllowed) на async-SSE хендлер: 200 + `Content-Type: text/event-stream`, heartbeat-кадр `: ping\n\n` каждые `sse.heartbeat_interval_sec`, освобождение по дисконнекту; анонимный доступ, agent_id из middleware-контекста в лог открытия, счётчик `voterpool_mcp_get_streams_total{outcome="opened"}`. DELETE остаётся 405. Проверка: сборка зелёная
- [x] 1.2 Вынести запись heartbeat/закрытие в переиспользуемый helper рядом с SseHub, если дублирование с `/mcp/events` превышает тривиальное. Проверка: нет копипасты логики таймера в двух местах

## 2. E2E-тесты

- [x] 2.1 В `tests/e2e/test_protocol.cpp`: сценарий GET /mcp → статус 200, content-type text/event-stream, получение heartbeat-кадра за разумное время (интервал теста ускоряется конфигом), отсутствие JSON-RPC сообщений в кадрах. Проверка: тест зелёный
- [x] 2.2 Сценарий невалидного Bearer-токена на GET /mcp → 200 и поток открывается; DELETE /mcp → 405 (регресс); полный handshake стандартного клиента дополнен шагом GET после notifications/initialized. Проверка: тесты зелёные

## 3. Документация и спека

- [x] 3.1 `docs/05-mcp-contracts.md` §1.0.2: пункт «GET /mcp → 405» заменить на описание keepalive-потока (анонимно, только heartbeat, без endpoint/session), DELETE — 405; снять упоминание «standalone-режима по 405». Проверка: текст согласован со спекой
- [x] 3.2 Дельта specs/mcp-protocol уже описывает новое поведение — сверить формулировки docs/05 и спеки между собой. Проверка: противоречий нет

## 4. Верификация

- [x] 4.1 Полный прогон `cmake --build build && ctest` — все suites зелёные. Проверка: ctest 100% pass
- [x] 4.2 Ручной кейс: поднять сервис, выполнить curl-эмуляцию клиента ревизии 2025-* (initialize → notification → tools/list → GET /mcp держится с heartbeat), убедиться что ни один шаг не даёт не-2xx. Проверка: лог без ошибок, поток живой

## 5. Диагностика и переговоры версии (воспроизведение с opencode)

- [x] 5.1 Локально воспроизвести отказ с настоящим opencode (`opencode mcp list`, таймаут 30с) и добавить диагностические логи: POST /mcp enter (ua/accept/bytes), GET-поток с UA, heartbeat-отказы с kind/age, закрытие стрима пиром с возрастом. Проверка: воспроизведён точный профиль отказа из логов VPS
- [x] 5.2 Трассировка socat: зафиксировать корень — initialize отвечает незнакомой клиенту версией ("2026-07-28" на запрос "2025-11-25") → обязательный дисконнект клиента. Проверка: wire-дамп с обеими сторонами обмена
- [x] 5.3 Исправить переговоры: fallback = ближайшая младшая поддерживаемая; дефолт supported_versions += "2025-11-25" (Config.h, default.yaml, docs/08, docs/05); спека-дельта handshake обновлена. Проверка: unit/e2e тесты переговоров зелёные
- [x] 5.4 Финальная проверка: `opencode mcp list` показывает voterpool connected; полный ctest зелёный. Проверка: статус ✓ connected в выводе opencode
