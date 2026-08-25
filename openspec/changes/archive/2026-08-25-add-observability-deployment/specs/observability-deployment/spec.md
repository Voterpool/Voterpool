## Purpose

Воспроизводимый self-hosted скаффолдинг наблюдаемости на одном хосте: стек Prometheus + Grafana (+ node_exporter, alertmanager, Loki/Promtail) разворачивается декларативно одной командой в двух модах (всё в контейнерах / приложение на хосте), согласован провижинингом без ручных действий, реализует алерты docs/11 §5 с зафиксированными стартовыми порогами и соблюдает периметр безопасности анонимных эндпоинтов (docs/03 §1.2, docs/15).

## ADDED Requirements

### Requirement: Однокомандный запуск стека

Из директории `deploy/` команда `docker compose up -d` ДОЛЖНА поднимать полный стек (voterpool, prometheus, grafana, node_exporter, alertmanager, loki, alloy) в моде B, а `docker compose down` — останавливать его с сохранением данных в named volumes. Порядок старта ДОЛЖЕН определяться healthcheck'ами там, где образ содержит утилиты проверки (prometheus, alertmanager, grafana); живость voterpool (distroless без shell/wget) ДОЛЖНА контролироваться скрейпом Prometheus — алерт `VoterpoolScrapeDown` (`up{job="voterpool"} == 0`) обязателен как компенсирующий контроль.

#### Scenario: Холодный старт моды B

- **WHEN** выполняется `cp .env.example .env && docker compose up -d` из `deploy/`
- **THEN** все семь сервисов переходят в состояние healthy без ручных действий

#### Scenario: Перезапуск с сохранением данных

- **WHEN** выполняется `docker compose down && docker compose up -d` после часа работы со скрейпами
- **THEN** исторические метрики Prometheus и настройки Grafana доступны после рестарта (named volumes не удалены)

### Requirement: Упаковка приложения в образ (мода B)

Скаффолд ДОЛЖЕН содержать Dockerfile, упаковывающий статически собранный бинарник voterpool и прод-конфиг (`storage.path=/data`, метрики включены) в минимальный образ (distroless-класс). Данные хранилища ДОЛЖНЫ жить в named volume на `/data`; логи приложения ДОЛЖНЫ писаться в stdout/stderr.

#### Scenario: Запуск контейнера приложения

- **WHEN** собирается образ через `docker build -f deploy/voterpool.Dockerfile` после `./build.sh` и запускается compose-стек
- **THEN** контейнер voterpool отвечает `GET /health` = 200 внутри сети compose и пишет JSON-free key=value строки запросов в stdout

#### Scenario: Деградация хранилища видна в health

- **WHEN** том `/data` недоступен для записи и контейнер перезапущен
- **THEN** `GET /health` отвечает HTTP 503 и `voterpool_db_healthy == 0` присутствует в ответе `/metrics`

### Requirement: Режим «бинарник на хосте» (мода A)

Скаффолд ДОЛЖЕН поддерживать запуск observability-стека без контейнеризации приложения через compose override: сервис voterpool исключается, Prometheus скрейпит `host.docker.internal:8080`, для linux ДОЛЖЕН быть задан `extra_hosts: ["host.docker.internal:host-gateway"]`.

#### Scenario: Стек поверх systemd-приложения

- **WHEN** voterpool запущен на хосте на порту 8080 и выполнен `docker compose -f docker-compose.yml -f docker-compose.hostmode.yaml up -d`
- **THEN** цель `host.docker.internal:8080` в Prometheus переходит в состояние UP и метрики voterpool доступны в Grafana

### Requirement: Скрейпинг метрик

Prometheus ДОЛЖЕН скрейпить `GET /metrics` voterpool со стандартным интервалом (15s) по внутренней сети compose (мода B) или через host-gateway (мода A) и дополнительно скрейпить node_exporter. Цель voterpool ДОЛЖНА быть доступна для скрейпа с первого опроса после старта: все семейства каталога docs/11 §3 экспонируются с нулевыми значениями сразу после инициализации процесса.

#### Scenario: Первый скрейп детерминирован

- **WHEN** voterpool стартовал и Prometheus выполнил первый успешный скрейп
- **THEN** в `/api/v1/label/__name__/values` присутствуют семейства `voterpool_mcp_requests_total`, `voterpool_db_healthy`, `voterpool_sse_queue_depth` и остальные каталога docs/11 §3

### Requirement: Автопровижининг Grafana

Grafana при старте ДОЛЖНА автоматически, без ручных действий в UI: иметь datasource `prometheus` (url `http://prometheus:9090`, по умолчанию), загружать все дашборды скаффолда из смонтированной папки провижининга и не требовать ввода учётных данных источника данных.

#### Scenario: Первый вход оператора

- **WHEN** оператор открывает `http://127.0.0.1:3000` и входит под admin-паролем из `.env`
- **THEN** datasource prometheus уже настроен и проверку соединения проходит, все четыре дашборда перечислены в списке без импорта вручную

### Requirement: Дашборды каталога метрик

Провижининг ДОЛЖЕН включать три дашборда voterpool строго из существующих семейств docs/11 §3 — «Voterpool MCP API» (rate запросов по outcome/name, ошибки по кодам, p50/p99 длительности POST /mcp, доля clientInfo), «Consensus» (голоса по decision, активные proposals, закрытия по final_status, early-exit по модели, применённые actions), «Storage & SSE» (db_healthy, набор voterpool_rocksdb_*, ошибки записи БД, SSE-соединения/очередь/ошибки записи, TTL-сканы и их длительность, агенты/организации) — плюс хостовый дашборд node_exporter (CPU, память, диск, сеть).

#### Scenario: Покрытие критических панелей

- **WHEN** открыт дашборд «Storage & SSE» на работающем стеке
- **THEN** панели `voterpool_db_healthy`, block cache usage/capacity, pending compaction bytes, `voterpool_sse_queue_depth` отображают ненулевые данные за последний час работы

### Requirement: Алерты docs/11 §5 со стартовыми порогами

Rules-файл Prometheus ДОЛЖЕН транскрибировать алерты docs/11 §5 с конкретными стартовыми порогами:

Алерты:
- `VoterpoolStorageDegraded`: `voterpool_db_healthy == 0`, severity critical, `for: 30s` (два скрейпа дебаунса — страница немедленно)
- `VoterpoolHttpLatencyHigh`: p99 `voterpool_http_request_duration_seconds` > 1s, `for: 10m`, severity warning
- `VoterpoolRpcErrorsSurge`: суммарный rate `voterpool_rpc_errors_total[5m]` > 0.1 rps, `for: 10m`, severity warning
- `VoterpoolSseQueueBacklog`: `voterpool_sse_queue_depth` > 5000, `for: 5m`, severity warning
- `VoterpoolTtlScanSlow`: p99 `voterpool_ttl_scan_duration_seconds` > 1s, `for: 15m`, severity warning
- `VoterpoolVotesExpiredSpike`: `increase(voterpool_proposals_closed_total{final_status="EXPIRED"}[1h])` > 5, `for: 15m`, severity warning

Пороги ЯВЛЯЮТСЯ стартовыми и калибруются по живым данным; правила ДОЛЖНЫ загружаться Prometheus при старте без ошибок (`promtool check rules` чист).

#### Scenario: Критический алерт деградации

- **WHEN** voterpool переведён в деградированный режим хранилища
- **THEN** алерт `VoterpoolStorageDegraded` переходит в firing в течение минуты и доставляется в alertmanager

#### Scenario: Валидность правил

- **WHEN** правила прогнаны через `promtool check rules`
- **THEN** ошибок нет

### Requirement: Сбор логов

Коллектор логов (Grafana Alloy) ДОЛЖЕН обнаруживать контейнеры стека через docker discovery, парсить формат spdlog `[timestamp] [level] [thread] message` (docs/08) и отправлять записи в Loki с лейблами только из ограниченных множеств (`job`, `container`, `level`). Идентификаторы (`agent_id`, `request_id`) ДОЛЖНЫ оставаться в теле записи и ЗАПРЕЩЕНЫ как значения лейблов.

#### Scenario: Поиск инцидента по уровню

- **WHEN** в Grafana Explore выбран label `level="error"` за последний час
- **THEN** записи логов voterpool с телом (включая agent_id=…) возвращаются

### Requirement: Периметр безопасности

Скаффолд ДОЛЖЕН привязывать порты наблюдения (grafana 3000, prometheus 9090, alertmanager 9093, loki 3100) к `127.0.0.1` хоста; скрейп метрик voterpool ДОЛЖЕН идти только по внутренней сети compose (мода B) или host-gateway (мода A). Анонимные `/metrics` и `/health` НЕ ДОЛЖНЫ публиковаться наружу напрямую. Секреты (пароль Grafana) ДОЛЖНЫ задаваться только через `.env`, отсутствующий в git; репозиторий ДОЛЖЕН содержать `.env.example` без реальных секретов.

#### Scenario: Наблюдение не торчит наружу

- **WHEN** со внешнего интерфейса хоста выполняется подключение к портам 3000/9090/3100/9093
- **THEN** соединение отклонено (bind только на loopback)

#### Scenario: Секреты не коммитятся

- **WHEN** выполняется `git status` после локальной настройки `.env`
- **THEN** файл `.env` отсутствует в изменениях (игнорируется)

### Requirement: Ретеншн и пиннинг версий

Prometheus ДОЛЖЕН работать с retention 15 дней И капом по размеру диска; Loki — с retention 30 дней и включённым компактором. Все образы стека ДОЛЖНЫ быть закреплены конкретными версиями (теги вида vX.Y.Z / стабильный мажор; тег `latest` ЗАПРЕЩЁН).

#### Scenario: Диск защищён от переполнения

- **WHEN** объём данных Prometheus достигает size-капа
- **THEN** самые старые сегменты вытесняются, запись новых метрик продолжается

#### Scenario: Воспроизводимость версий

- **WHEN** читается `deploy/docker-compose.yml`
- **THEN** каждый `image:` содержит конкретную версию без `latest`
