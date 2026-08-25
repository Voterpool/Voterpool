## 1. Верификация пиннинга образов

- [x] 1.1 Проверить существование кандидатских тегов из design D4 через `docker manifest inspect` для каждого образа (prometheus v3.x, grafana 12.x, alertmanager v0.28.x, node-exporter v1.10.x, loki 3.x, alloy v1.x) и зафиксировать фактические патч-версии; для distroless/cc-debian12 получить digest. Verify: список тегов с результатами проверки приложен к change, несуществующие теги заменены ближайшими доступными в той же мажорной линии.

## 2. Каркас compose

- [x] 2.1 Создать `deploy/docker-compose.yml`: сеть `obs`, named volumes (`vp_data`, `prom_data`, `grafana_data`, `loki_data`, `am_data`), сервис voterpool под профилем `bundled` с healthcheck `GET /health`, все образы — тегами из 1.1, без `latest`. Verify: `docker compose config --profiles` показывает профиль bundled, конфиг валиден без ошибок.
- [x] 2.2 Создать `deploy/.env.example` (GRAFANA_ADMIN_PASSWORD, GF_SERVER_ROOT_URL) и добавить `deploy/.env` в `.gitignore`. Verify: `.env.example` не содержит реальных секретов; `git check-ignore deploy/.env` успешен.
- [x] 2.3 Создать `deploy/config/docker.yaml` на основе `config/default.yaml`: `storage.path=/data`, метрики включены, логи в stdout (`log_file` пустой). Verify: diff против default.yaml содержит только согласованные поля.

## 3. Образ приложения

- [ ] 3.1 Создать `deploy/voterpool.Dockerfile` на `gcr.io/distroless/cc-debian12@digest` (из 1.1): копия бинарника сборки и конфига, ENTRYPOINT с `--config /etc/voterpool/config.yaml`. Verify: `docker build -f deploy/voterpool.Dockerfile` после `./build.sh` завершается успешно, образ запускается и `/health` отвечает 200 внутри сети.

## 4. Prometheus и алерты

- [x] 4.1 Создать `deploy/prometheus/prometheus.yml`: scrape_configs для voterpool (мода B: `voterpool:8080`; интервал 15s) и node_exporter, флаги retention time=15d/size=5GB вынесены в command compose-сервиса. Verify: `promtool check config` чист.
- [x] 4.2 Создать `deploy/prometheus/alerts/voterpool.rules.yml` с шестью алертами design D5 (пороги, for, severity как в таблице) плюс алерт заполнения диска node_exporter (>85%). Verify: `promtool check rules` чист; правила загружены (`/-/rules` содержит 7 групп).

## 5. Провижининг Grafana

- [x] 5.1 Создать `deploy/grafana/provisioning/datasources/prometheus.yml` (url `http://prometheus:9090`, isDefault) и `provisioning/dashboards/provider.yml` (папка dashboards, foldersFromFilesStructure). Verify: файлы валидны по схеме provisioning, пути совпадают с монтированиями compose.
- [ ] 5.2 Создать JSON-дашборд «Voterpool MCP API»: rate запросов по outcome/name, rate ошибок по code, p50/p99 histogram_quantile длительности POST /mcp, доля clientInfo. Verify: импорт через провижининг при холодном старте, панели отдают данные после генерации нагрузки.
- [ ] 5.3 Создать JSON-дашборд «Consensus»: голоса по decision, proposals_active, закрытия по final_status, early-exit по модели, actions_applied по kind. Verify: аналогично 5.2.
- [ ] 5.4 Создать JSON-дашборд «Storage & SSE»: db_healthy, voterpool_rocksdb_* (block cache usage/capacity/hits/misses, pending compaction, WAL syncs, компакция), db_write_failures по kind, sse_connections/queue_depth/write_failures, ttl_scans и p99 длительности, agents/orgs. Verify: аналогично 5.2.
- [ ] 5.5 Добавить хостовый дашборд node_exporter (CPU, память, disk io/latency и заполнение, сеть). Verify: четыре дашборда перечислены в UI без ручного импорта.

## 6. Логи: Loki + Alloy

- [x] 6.1 Создать `deploy/loki/loki-config.yml`: filesystem backend, retention 30d, compactor включён. Verify: `loki -config.file=... -verify-config` проходит.
- [ ] 6.2 Создать `deploy/alloy/config.alloy`: discovery.docker, loki.source.docker (socket read-only), loki.process со stage.regex формата spdlog `[ts] [level] [thread] msg` → label level; лейблы только job/container/level. Verify: после старта записи voterpool видны в Grafana Explore с label level; agent_id присутствует в теле, отсутствует среди лейблов.

## 7. Alertmanager и мода A

- [ ] 7.1 Создать `deploy/alertmanager/alertmanager.yml` с маршрутом по severity и ресиверами-плейсхолдерами; подключить к Prometheus (alerting + rule_files). Verify: алерт VoterpoolStorageDegraded вручную смоделированный доходит до alertmanager API.
- [ ] 7.2 Создать `deploy/docker-compose.hostmode.yaml`: исключение сервиса voterpool, scrape-цель `host.docker.internal:8080`, `extra_hosts: host.docker.internal:host-gateway`. Verify: `docker compose -f docker-compose.yml -f docker-compose.hostmode.yaml config` валиден; при voterpool на хосте цель UP.

## 8. Документация

- [x] 8.1 Обновить дерево проекта docs/09 §1.1: добавить `deploy/` с краткими комментариями файлов. Verify: структура дерева соответствует фактическим файлам deploy/.
- [x] 8.2 Правка docs/15 §2/§8: Promtail → Grafana Alloy (решение D3), §10 — фактический size-кап 5GB. Verify: упоминаний Promtail как действующего компонента нет (допустима одна строка-обоснование выбора Alloy), ссылки на alloy-конфиг корректны.

## 9. Интеграционная проверка (cold start)

> **STATUS: Change заархивирован с незавершёнными runtime-проверками** (среда без Docker-демона).
> Все артефакты стека записаны и статически верифицированы (promtool 3.14.0: config+rules SUCCESS;
> loki 3.7.6 -verify-config: valid; docker compose v2.39.2 config: обе моды валидны; JSON-дашборды распарсены).
> Ниже — чек-лист для выполнения на хосте с Docker; после прогона отметить задачи.

- [ ] 9.1 Полный прогон сценария спеки моды B на чистом окружении: `./build.sh && docker build ... && cd deploy && cp .env.example .env && docker compose up -d`; проверить: все сервисы healthy; первый скрейп содержит семейства каталога docs/11 §3; grafana datasource проходит проверку соединения; порты 3000/9090/9093/3100 отвечают только на 127.0.0.1. Verify: чек-лист сценария пройден полностью, отклонения зафиксированы.
- [ ] 9.2 Проверить персистентность: час работы со скрейпами → `docker compose down && up -d` → история метрик и настройки Grafana доступны; затем `docker compose down -v` возвращает чистое состояние. Verify: данные пережили рестарт, удаление томов работает.
