## Why

У voterpool есть полный экспорт метрик (`GET /metrics`, каталог docs/11 §3), health-эндпоинт и структурированные логи, но нет ни одного артефакта развертывания observability-стека: ни Dockerfile, ни compose-файла, ни провижининга Grafana. Self-hosted пользователь должен собирать связку Prometheus + Grafana вручную — это противоречит философии продукта «download and run» и решению, зафиксированному в docs/15.

## What Changes

- Добавляется директория `deploy/` со скаффолдом полного стека (M1+M2+M3 из docs/15 §2): docker-compose.yml, pinned-образы, `.env.example`, конфиг приложения для контейнера.
- Добавляется `deploy/voterpool.Dockerfile`: упаковка статического бинарника в distroless-образ (мода B — всё в compose, основная).
- Добавляется режим «бинарник на хосте» (мода A) через compose override с скрейпом через `host.docker.internal:host-gateway`.
- Prometheus получает scrape-конфигурацию (voterpool, node_exporter) и rules-файл с алертами — транскрипция docs/11 §5 с конкретными SLA-порогами.
- Grafana получает автопровижининг при старте: datasource prometheus, provider дашбордов и три JSON-дашборда под каталог метрик docs/11 §3 (MCP API, Consensus, Storage & SSE) + хостовый дашборд node_exporter.
- Добавляются alertmanager (ресиверы-плейсхолдеры) и Loki + Promtail (сбор stdout-логов контейнеров, лейбл level, дисциплина кардинальности логов из docs/11 §1).
- Порты наблюдения привязываются к 127.0.0.1 / внутренней docker-сети; секреты — только через `.env`.
- Дерево проекта в docs/09 §1.1 дополняется записью о `deploy/`.

Код C++ НЕ меняется. Каталог метрик и поведение `/metrics`, `/health` не меняются (спецификация observability остаётся нетронутой).

## Capabilities

### New Capabilities

- `observability-deployment`: воспроизводимый скаффолдинг self-hosted observability-стека на одном хосте — compose-топология обеих мод, согласованный провижининг Prometheus ↔ Grafana, алерты docs/11 §5 с зафиксированными порогами, сбор логов, ретеншн и безопасность периметра (docs/15).

### Modified Capabilities

_(нет — поведение приложения не меняется)_

## Impact

- Новые файлы: `deploy/**` (~20 файлов: compose, Dockerfile, конфиги prometheus/grafana/alertmanager/loki/promtail, 4 JSON-дашборда, .env.example).
- Изменённые файлы: `docs/09-project-structure.md` (дерево §1.1), возможно `.gitignore` (добавить `deploy/.env`, `build/` уже покрыт).
- Зависимости среды: docker + docker compose v2 на целевом хосте; для моды B дополнительно запуск `./build.sh` перед сборкой образа.
- Риски: пиннинг версий образов устареет со временем (осознанное обновление через diff); SLA-пороги алертов стартовые, калибруются по живым данным после внедрения.
