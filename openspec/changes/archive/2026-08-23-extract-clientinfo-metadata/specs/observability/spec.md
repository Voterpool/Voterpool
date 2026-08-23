# Delta: observability

## MODIFIED Requirements

### Requirement: Каталог метрик Prometheus

GET /metrics ДОЛЖЕН быть анонимным, возвращать text/plain; version=0.0.4 (# HELP/# TYPE + семплы) и экспонировать каталог метрик docs/11 §3, включая как минимум: voterpool_mcp_requests_total {method, name, outcome}, voterpool_rpc_errors_total {code}, voterpool_http_request_duration_seconds, voterpool_cast_vote_duration_seconds, voterpool_votes_cast_total {decision}, voterpool_proposals_created_total, voterpool_proposals_closed_total {final_status}, voterpool_proposals_active, voterpool_consensus_early_exit_total {consensus_model}, voterpool_actions_applied_total {kind}, voterpool_agents_total, voterpool_orgs_active, voterpool_orgs_dissolved_total, voterpool_sse_connections, voterpool_sse_events_sent_total {event_type}, voterpool_sse_queue_depth, voterpool_sse_write_failures_total {event_type}, voterpool_ttl_scans_total, voterpool_ttl_scan_duration_seconds, voterpool_schema_migration_records_total {from,to}, voterpool_db_write_failures_total {kind}, voterpool_db_healthy, voterpool_mcp_client_meta_total {present}. Эндпоинт ДОЛЖЕН конфигурироваться (enabled, path) и оставаться доступным при деградации хранилища.

#### Scenario: Скрейп метрик

- **WHEN** выполняются MCP-вызовы, затем GET /metrics
- **THEN** ответ — валидный текстовый формат Prometheus; voterpool_mcp_requests_total увеличился относительно вызовов

#### Scenario: Метрики доступны при деградации

- **WHEN** db_healthy = false
- **THEN** GET /metrics продолжает отвечать 200 со свежими значениями (включая voterpool_db_healthy = 0)

#### Scenario: Учёт наличия clientInfo в запросах

- **WHEN** POST /mcp обработан с `_meta["io.modelcontextprotocol/clientInfo"]` и без него
- **THEN** voterpool_mcp_client_meta_total содержит семплы present="true" и present="false", увеличивающиеся соответственно; имя/версия клиента НЕ появляются в лейблах

### Requirement: Структурированное логирование запросов

Каждый запрос ДОЛЖЕН логироваться с request_id, agent_id (если авторизован), методом/инструментом, исходом (ok/error) и длительностью; поле clientInfo из _meta ДОЛЖНО попадать в логи. Токены api_key НЕ ДОЛЖНЫ попадать в логи. Значения clientInfo ДОЛЖНЫ санитизироваться и обрезаться до безопасной длины перед записью.

#### Scenario: Корреляция запросов в логах

- **WHEN** обрабатываются вызовы разных агентов
- **THEN** каждая запись содержит request_id, agent_id, инструмент, исход и длительность; секретные токены отсутствуют

#### Scenario: ClientInfo в записи лога

- **WHEN** запрос несёт `_meta["io.modelcontextprotocol/clientInfo"]` с name и version
- **THEN** запись лога содержит эти name и version; при их отсутствии поля записываются как прочерк

#### Scenario: Отклонение до диспетчеризации также логируется

- **WHEN** middleware отклоняет POST /mcp с -32600 или -32001
- **THEN** создаётся запись лога с request_id, исходом error и кодом отказа; тело и токены в запись не попадают

#### Scenario: Санитизация значений клиента

- **WHEN** clientInfo.name содержит управляющие символы или превышает допустимую длину
- **THEN** в лог попадает очищенное значение одной строкой, обрезанное до лимита
