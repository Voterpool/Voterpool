# Extract ClientInfo Metadata

## Why

Спека mcp-protocol («Метаданные клиента _meta») и observability («Структурированное логирование запросов») требуют: `_meta.io.modelcontextprotocol/clientInfo` извлекается для логов и метрик; каждый запрос логируется с request_id, agent_id, методом/инструментом, исходом, длительностью; токены не логируются. Эти утверждения повторяют docs/03 §1.3, docs/05 §1.0 и docs/11 — но в коде clientInfo не читается ни разу, а структурное логирование запросов отсутствует целиком (только startup/error строки). Аудит после архивации трёх исправлений выявил разрыв; настоящее изменение закрывает его.

## What Changes

- Структурная строка лога (spdlog info) на каждый обработанный POST /mcp: request_id, agent_id (или "-"), method + tool, outcome ok/error с кодом ошибки, длительность ms, clientInfo {name, version} при наличии (санитизация + обрезка). Токены не попадают в лог.
- Лог-строки также на отклонениях middleware (-32600/-32001) — чтобы «каждый запрос» покрывал и отказы до диспетчеризации.
- Новый счётчик `voterpool_mcp_client_meta_total{present=true|false}` — ограниченная кардинальность (без client name в лейблах), закрывает половину «для метрик» требования mcp-protocol.
- Генерация request_id: лёгкий процессно-уникальный идентификатор без внешних зависимостей.
- Документация: docs/11 — строка новой метрики и пример лог-записи; формулировки docs/03/05 становятся фактически верными (правки не требуются).
- Тесты: unit на форматтер лог-строки (поля, красные зоны токенов, обрезка/санитизация clientInfo) и генератор request_id; e2e на счётчик present=true/false через /metrics; регресс полного набора.

## Capabilities

### New Capabilities

(нет)

### Modified Capabilities

- `observability`: каталог метрик дополняется счётчиком `voterpool_mcp_client_meta_total{present}`; требование структурного логирования дополняется сценариями покрытия отклонений middleware и санитизации clientInfo (само нормативное ядро требования не меняется — код приводится в соответствие с ним).

## Impact

- `src/mcp/McpHandler.cpp` — извлечение clientInfo из уже распарсенного тела, вызовы нового форматтера, инкремент счётчика.
- `include/mcp/McpHandler.h` / новые helpers (request_id, sanitize/format) — размещение согласуется со structure docs/09.
- `src/server/AuthMiddleware.cpp` — лог-строка на путях rejectProtocol и unauthorized.
- `docs/11-observability.md` — новая строка в таблице метрик, пример записи лога.
- `tests/unit/`, `tests/e2e/test_protocol.cpp` — новые тесты; существующий набор должен остаться зелёным.
