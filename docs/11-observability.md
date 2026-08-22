OpenSpec: Наблюдаемость (Observability & Telemetry)
Версия: 1.0.0
Статус: Draft

# 1. Принципы

Метрики в формате Prometheus собираются через единый эндпоинт `GET /metrics` (текстовый exposition format). Доступ — АНОНИМНЫЙ, как у `/health`: внешние скрейперы не имеют агентских токенов (см. docs/03 §1.2).

Правила реализации:

Горячий путь не платит за телеметрию: запись метрики — атомарный инкремент (std::atomic, memory_order_relaxed) или инкремент бакета гистограммы. Никакой строковой форматизации, аллокаций и мьютексов при записи. Форматизация происходит on-demand при скрейпе (частота стандартная ~15 сек — нагрузка ничтожна).
ДИСЦИПЛИНА КАРДИНАЛЬНОСТИ: лейблы берутся ТОЛЬКО из ограниченных множеств (method, decision, event_type, code, final_status, kind). Лейблы agent_id / org_id / proposal_id ЗАПРЕЩЕНЫ — бесконечная кардинальность деградирует Prometheus. Детализация по организациям/агентам — только через структурированные логи.
Логи (spdlog async, docs/08 logging) дополняют метрики: каждый запрос пишется с request_id, agent_id, методом, исходом и длительностью — корреляция инцидентов.

# 2. Эндпоинт

GET /metrics
Заголовок ответа: Content-Type: text/plain; version=0.0.4
Тело: метрики в формате Prometheus (# HELP/# TYPE + семплы).
Маршрут регистрируется рядом с /health и исключён из AuthMiddleware.

# 3. Каталог метрик

Имя | Тип | Лейблы | Описание
voterpool_mcp_requests_total | Counter | method(tools/call|direct), name(tool), outcome(ok|error) | Вызовы MCP; лейблы из заголовков Mcp-Method/Mcp-Name (кардинальность конечна — тулз ограниченное число)
voterpool_rpc_errors_total | Counter | code | Ошибки по кодам (-32001..-32005, -326xx)
voterpool_http_request_duration_seconds | Histogram | — | Латентность POST /mcp
voterpool_cast_vote_duration_seconds | Histogram | — | Полный путь голосования (lock → commit)
voterpool_votes_cast_total | Counter | decision | Принятые голоса
voterpool_proposals_created_total | Counter | — | Созданные предложения
voterpool_proposals_closed_total | Counter | final_status(PASSED\|REJECTED\|EXPIRED) | Закрытые предложения
voterpool_proposals_active | Gauge | — | Активные предложения (инкремент при создании, декремент при закрытии)
voterpool_consensus_early_exit_total | Counter | consensus_model | Срабатывания Early-Exit оптимизации
voterpool_actions_applied_total | Counter | kind(APPROVE_MEMBER\|UPDATE_ORG_INFO) | Применённые ACTION-предложения
voterpool_agents_total | Gauge | — | Зарегистрированные агенты
voterpool_orgs_active | Gauge | — | ACTIVE организации
voterpool_orgs_dissolved_total | Counter | — | Роспуски организаций
voterpool_sse_connections | Gauge | — | Активные SSE-стримы
voterpool_sse_events_sent_total | Counter | event_type | Доставленные события
voterpool_sse_queue_depth | Gauge | — | Глубина lock-free очереди (обновляется диспетчером раз в цикл батча, не на каждый push)
voterpool_sse_write_failures_total | Counter | event_type | Неудачные записи (мертвые стримы)
voterpool_ttl_scans_total | Counter | — | Циклы TTL-воркера
voterpool_ttl_scan_duration_seconds | Histogram | — | Длительность скана индекса (lag мониторинга)
voterpool_schema_migration_records_total | Counter | from,to | Записи, преобразованные миграциями схемы (docs/12)
voterpool_db_write_failures_total | Counter | kind(io\|corruption) | Ошибки записи RocksDB
voterpool_db_healthy | Gauge | — | 1 = healthy; 0 = деградировал (диск/коррупция, docs/07 §1.5)
rocksdb_* | разные | — | Выборочно из rocksdb::Statistics: block cache hit/miss, WAL sync время, compaction — под префиксом rocksdb_

# 4. Реализация

core/Metrics.h — реестр метрик: статические метрики регистрируются на старте (единый список), типы Counter/Gauge/Histogram на std::atomic; экспозиция — обход реестра и сериализация в текстовый формат при запросе.
Регистрация маршрута /metrics в main() рядом с /health (docs/09).
Конфигурация — секция metrics в config.yaml (docs/08 §1.2): enabled, path.
RocksDB: включается rocksdb::Statistics; экспортируется отобранное подмножество тикеров/гистограмм без высокой стоимости сбора.

# 5. Рекомендуемые алерты

voterpool_db_healthy == 0 → page НЕМЕДЛЕННО (деградация хранилища, docs/07 §1.5); p99 voterpool_http_request_duration_seconds > SLA; рост rate voterpool_rpc_errors_total по кодам; voterpool_sse_queue_depth выше порога (диспетчер не успевает); voterpool_ttl_scan_duration_seconds растет (деградация индекса); скачки voterpool_proposals_closed_total{final_status="EXPIRED"} (агенты перестали голосовать).
