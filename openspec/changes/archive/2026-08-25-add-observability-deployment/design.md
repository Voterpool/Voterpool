## Context

Приложение экспортирует полный каталог метрик через анонимный `GET /metrics` и пишет структурированные логи (async spdlog, формат `[ts] [level] [thread] key=value`) в stdout — см. proposal.md. Артефактов развертывания нет. Решение по топологии зафиксировано в docs/15: один хост + Docker Compose, обе моды, стек M1+M2+M3. K8s вне скопа; артефакты переносимы в kube-prometheus-stack позже.

## Goals / Non-Goals

**Goals:**
- Воспроизводимый холодный старт полного стека одной командой ≤ 5 минут на чистом VPS с docker.
- Согласованность Prometheus ↔ Grafana без ручных действий (провижининг).
- Конкретные стартовые SLA-пороги алертов вместо плейсхолдеров docs/11 §5.
- Периметр безопасности: наблюдение не публикуется наружу, секреты вне git.

**Non-Goals:**
- TLS reverse proxy (Caddy/nginx) — документируется как ручной шаг, конфигурация не включается.
- HA, кластеризация, multi-host discovery, cadvisor (контейнерные метрики не требуются).
- Изменения кода приложения C++, каталога метрик или конфигурационных секций приложения.
- Автоматическое обновление образов (bump — осознанный git diff).

## Decisions

### D1. Compose без k8s-обвязки, мода A — override-файлом

Основной `docker-compose.yml` содержит все 7 сервисов (мода B). Мода A — `docker-compose.hostmode.yaml` с `deploy/voterpool` удалённым (`profiles: ["bundled"]` для сервиса приложения) и scrape-целью `host.docker.internal:8080` + `extra_hosts: ["host.docker.internal:host-gateway"]`. Альтернатива (два независимых compose) отклонена: дублирование провижининга расходит со временем.

### D2. Базовый образ приложения — `gcr.io/distroless/cc-debian12`

Бинарник собирается на хосте штатным `./build.sh` (тулчейн vcpkg/CMake в образ не тащим), копируется вместе с `deploy/config/docker.yaml`. distroless cc (не scratch): бинарник статически связан, но cc-debian12 даёт CA-сертификаты и libc-страховку при цене ~2 МБ; отладочная оболочка отсутствует — соответствует прод-профилю. Тег пиннуется по digest. Альтернатива multi-stage сборка в образе отклонена: время сборки RocksDB в контейнере неприемлемо, воспроизводимость тулчейна хуже.

### D3. Коллектор логов — Grafana Alloy, не Promtail

Promtail объявлен EOL (март 2026) — для требования «последние LTS» берётся Grafana Alloy (docker mode): `discovery.docker` + `loki.source.docker` + `loki.process` со `stage.regex` по шаблону spdlog → label `level`. Alloy монтирует docker socket read-only. Альтернатива (docker logging driver loki-plugin) отклонена: требует установки плагина на хост вне compose и не позволяет pipeline-парсинг уровня. В tasks включена правка упоминаний Promtail в docs/15 §2/§8.

### D4. Пиннинг версий — конкретными тегами majors текущих LTS-линий

Верифицировано через registry API (hub.docker.com / gcr.io manifest headers), 2026-08-24:

Компонент | Образ : тег
prometheus | `prom/prometheus:v3.14.0`
grafana | `grafana/grafana:12.4.9` (последняя зрелая 12.x; 13.0 только что вышла — берётся позже осознанным bump)
alertmanager | `prom/alertmanager:v0.28.1`
node_exporter | `prom/node-exporter:v1.12.1`
loki | `grafana/loki:3.7.6`
alloy | `grafana/alloy:v1.19.0`
база voterpool | `gcr.io/distroless/cc-debian12@sha256:e5d81ddde149641e2a9ba55be4545bc125c67de07508b03ba4c22e6eb0ded5aa`

Теги зашиты в compose напрямую (не через переменные) — bump = осознанный diff, как решено в docs/15 §2.

### D5. Стартовые SLA-пороги алертов

Обоснование: движок детерминированный, нормальная латентность запроса — миллисекунды, поэтому p99 > 1s — уже аномалия, но не инцидент; деградация хранилища — всегда страница. Зафиксировано в спеке:

| Алерт | Порог | for | Severity |
|---|---|---|---|
| VoterpoolStorageDegraded | `voterpool_db_healthy == 0` | 30s (дебаунс 2 скрейпов) | critical |
| VoterpoolHttpLatencyHigh | p99 http duration > 1s | 10m | warning |
| VoterpoolRpcErrorsSurge | rate(rpc_errors_total[5m]) > 0.1 rps | 10m | warning |
| VoterpoolSseQueueBacklog | sse_queue_depth > 5000 | 5m | warning |
| VoterpoolTtlScanSlow | p99 ttl_scan_duration > 1s | 15m | warning |
| VoterpoolVotesExpiredSpike | increase(closed{EXPIRED}[1h]) > 5 | 15m | warning |
| VoterpoolScrapeDown | `up{job="voterpool"} == 0` | 1m | warning |

VoterpoolScrapeDown добавлен при имплементации: distroless-образ не содержит утилит для container healthcheck (спека §«Однокомандный запуск» поправлена соответственно) — живость процесса контролируется скрейпом. Плюс node-группа: заполнение ФС > 85%. Пороги калибруются по живым данным после внедрения; правила обязаны проходить `promtool check rules`.

### D6. Ресурсы и ретеншн

Prometheus: `--storage.tsdb.retention.time=15d` + `--storage.tsdb.retention.size=5GB`. Loki: retention 30d, compactor on, filesystem backend. Named volumes: `vp_data`, `prom_data`, `grafana_data`, `loki_data`, `am_data`. Ориентир RAM стека ~1–1.5 ГБ сверх voterpool.

### D7. Сеть и периметр

Единая bridge-сеть `obs`; наружу публикуются ТОЛЬКО `127.0.0.1:3000` (grafana) и порт агентов voterpool (модозависимо). Prometheus/alertmanager/loki портов наружу не имеют вовсе. Скрейп — по DNS имён сервисов внутри сети. Пароль Grafana из `.env` (`GRAFANA_ADMIN_PASSWORD`), `.env` в `.gitignore`.

## Risks / Trade-offs

- [Кандидатские теги образов могут не существовать/устареть] → первый task apply верифицирует теги через registry и фиксирует фактические; design задаёт только линии мажоров.
- [Alloy с доступом к docker socket — расширение привилегий при компрометации] → mount read-only, отдельная сеть, образ официальной подписи; в перспективе — syslog-driver без socket.
- [Соседство Prometheus и RocksDB на одном диске] → size-кап 5GB, панели disk io/latency в node-дашборде, алерт на заполнение диска node_exporter'ом добавлен в rules (>85%).
- [Regex парсинга spdlog сломается при смене формата логов] → лейблы job/container независимы от формата; записи с непарсящимся уровнем попадают в Loki без label level и видны как аномалия.
- [Grafana provisioning API меняется между мажорами] → мажор закреплён; cold-start сценарий спеки ловит регресс при bump.
- [Анонимный /metrics случайно опубликован] → мода B публикует только порт агентов; проверка биндов входит в сценарий «Наблюдение не торчит наружу».

## Migration Plan

Внедрение greenfield: новая директория `deploy/`, правки двух docs и `.gitignore` — код приложения не затрагивается. Откат: `docker compose down -v` и удаление `deploy/` полностью возвращает репозиторий в прежнее состояние. Обновление версий стека — точечный diff тегов с прогоном cold-start сценария.

## Open Questions

Нет — выбор reverse proxy (Caddy vs nginx) осознанно вынесен за скоп и на разбиение задач не влияет.
