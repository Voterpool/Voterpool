# Delta: observability

## MODIFIED Requirements

### Requirement: Каталог метрик Prometheus

GET /metrics ДОЛЖЕН быть анонимным, возвращать text/plain; version=0.0.4 (# HELP/# TYPE + семплы) и экспонировать каталог метрик docs/11 §3, включая как минимум: voterpool_mcp_requests_total {method, name, outcome}, voterpool_rpc_errors_total {code}, voterpool_http_request_duration_seconds, voterpool_cast_vote_duration_seconds, voterpool_votes_cast_total {decision}, voterpool_proposals_created_total, voterpool_proposals_closed_total {final_status}, voterpool_proposals_active, voterpool_consensus_early_exit_total {consensus_model}, voterpool_actions_applied_total {kind}, voterpool_agents_total, voterpool_orgs_active, voterpool_orgs_dissolved_total, voterpool_sse_connections, voterpool_sse_events_sent_total {event_type}, voterpool_sse_queue_depth, voterpool_sse_write_failures_total {event_type}, voterpool_ttl_scans_total, voterpool_ttl_scan_duration_seconds, voterpool_schema_migration_records_total {from,to}, voterpool_db_write_failures_total {kind}, voterpool_db_healthy, voterpool_mcp_client_meta_total {present}. Каталог ДОЛЖЕН также включать фиксированный набор гейджей voterpool_rocksdb_* из встроенной статистики хранилища (как минимум: block cache usage/capacity/hits/misses, оценка незавершённой компакции в байтах, число синхронизаций WAL, байты записи flush и компакции); эти семейства ДОЛЖНЫ экспонироваться без лейблов. Эндпоинт ДОЛЖЕН конфигурироваться (enabled, path) и оставаться доступным при деградации хранилища.

#### Scenario: Скрейп метрик

- **WHEN** выполняются MCP-вызовы, затем GET /metrics
- **THEN** ответ — валидный текстовый формат Prometheus; voterpool_mcp_requests_total увеличился относительно вызовов

#### Scenario: Метрики доступны при деградации

- **WHEN** db_healthy = false
- **THEN** GET /metrics продолжает отвечать 200 со свежими значениями (включая voterpool_db_healthy = 0)

#### Scenario: Учёт наличия clientInfo в запросах

- **WHEN** POST /mcp обработан с `_meta["io.modelcontextprotocol/clientInfo"]` и без него
- **THEN** voterpool_mcp_client_meta_total содержит семплы present="true" и present="false", увеличивающиеся соответственно; имя/версия клиента НЕ появляются в лейблах

#### Scenario: RocksDB-семейства присутствуют после нагрузки

- **WHEN** БД открыта, выполнены записи, затем выполняется скрейп GET /metrics
- **THEN** выдача содержит семейства voterpool_rocksdb_* с # HELP перед # TYPE и неотрицательными числовыми значениями без лейблов
