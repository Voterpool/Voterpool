## Purpose

Наблюдаемость: анонимные эндпоинты GET /health и GET /metrics (Prometheus text format), каталог метрик с дисциплиной кардинальности, структурированное логирование запросов и fail-fast деградированный режим хранилища (-32050/HTTP 503) (docs/11, docs/07 §1.5).

## ADDED Requirements

### Requirement: Health-эндпоинт

GET /health ДОЛЖЕН быть анонимным и отвечать 200 OK на здоровом движке и HTTP 503 в деградированном режиме хранилища.

#### Scenario: Здоровый движок

- **WHEN** GET /health при работающем хранилище
- **THEN** ответ HTTP 200

#### Scenario: Деградированное хранилище

- **WHEN** GET /health после перевода движка в деградированный режим
- **THEN** ответ HTTP 503

### Requirement: Каталог метрик Prometheus

GET /metrics ДОЛЖЕН быть анонимным, возвращать text/plain; version=0.0.4 (# HELP/# TYPE + семплы) и экспонировать каталог метрик docs/11 §3, включая как минимум: voterpool_mcp_requests_total {method, name, outcome}, voterpool_rpc_errors_total {code}, voterpool_http_request_duration_seconds, voterpool_cast_vote_duration_seconds, voterpool_votes_cast_total {decision}, voterpool_proposals_created_total, voterpool_proposals_closed_total {final_status}, voterpool_proposals_active, voterpool_consensus_early_exit_total {consensus_model}, voterpool_actions_applied_total {kind}, voterpool_agents_total, voterpool_orgs_active, voterpool_orgs_dissolved_total, voterpool_sse_connections, voterpool_sse_events_sent_total {event_type}, voterpool_sse_queue_depth, voterpool_sse_write_failures_total {event_type}, voterpool_ttl_scans_total, voterpool_ttl_scan_duration_seconds, voterpool_schema_migration_records_total {from,to}, voterpool_db_write_failures_total {kind}, voterpool_db_healthy. Эндпоинт ДОЛЖЕН конфигурироваться (enabled, path) и оставаться доступным при деградации хранилища.

#### Scenario: Скрейп метрик

- **WHEN** выполняются MCP-вызовы, затем GET /metrics
- **THEN** ответ — валидный текстовый формат Prometheus; voterpool_mcp_requests_total увеличился относительно вызовов

#### Scenario: Метрики доступны при деградации

- **WHEN** db_healthy = false
- **THEN** GET /metrics продолжает отвечать 200 со свежими значениями (включая voterpool_db_healthy = 0)

### Requirement: Дисциплина кардинальности лейблов

Лейблы метрик ДОЛЖНЫ браться только из ограниченных множеств (method, name, outcome, decision, final_status, kind, code, event_type, consensus_model, from/to, io/corruption). Лейблы agent_id, org_id, proposal_id ЗАПРЕЩЕНЫ.

#### Scenario: Отсутствие неограниченных лейблов

- **WHEN** инспектируется выдача /metrics
- **THEN** ни одна метрика не содержит лейблов agent_id/org_id/proposal_id

### Requirement: Деградированный режим хранилища

При ошибке записи/открытия/сброса хранилища класса IOError или Corruption движок ОБЯЗАН установить единый атомарный флаг нездоровья с критической записью лога. Пока флаг установлен, ВСЕ новые запросы POST /mcp ДОЛЖНЫ мгновенно отклоняться ошибкой -32050 Server Overloaded с HTTP 503 и data.reason "Storage backend unavailable" (проверка флага до разбора тела и любых обращений к БД); исключения — GET /health и GET /metrics. Выполняющиеся в момент сбоя запросы ДОЛЖНЫ завершаться штатно. Автовосстановление внутри процесса ОТСУТСТВУЕТ: флаг сбрасывается только рестартом процесса.

#### Scenario: Backpressure при деградации

- **WHEN** хранилище вернуло IOError (например, переполнен диск)
- **THEN** каждый новый POST /mcp немедленно получает -32050 и HTTP 503 без обращения к БД

#### Scenario: Нет автовосстановления

- **WHEN** диск освобождён, но процесс не перезапущен
- **THEN** движок остаётся в деградированном режиме (-32050) до рестарта

### Requirement: Структурированное логирование запросов

Каждый запрос ДОЛЖЕН логироваться с request_id, agent_id (если авторизован), методом/инструментом, исходом (ok/error) и длительностью; поле clientInfo из _meta ДОЛЖНО попадать в логи. Токены api_key НЕ ДОЛЖНЫ попадать в логи.

#### Scenario: Корреляция запросов в логах

- **WHEN** обрабатываются вызовы разных агентов
- **THEN** каждая запись содержит request_id, agent_id, инструмент, исход и длительность; секретные токены отсутствуют
