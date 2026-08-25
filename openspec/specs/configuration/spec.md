# Конфигурация (configuration)

## Purpose

Конфигурация приложения: загрузка config.yaml (YAML 1.2) с валидацией, переопределение любыми переменными окружения VOTERPOOL_{SECTION}_{KEY} и CLI-флагами, проверка прав доступа к директории хранилища, режим daemon (docs/08).

Область действия: редакция **On-Premises (Self-Hosted)**. Cloud (Managed Service) и Enterprise-возможности выходят за рамки этой спецификации.

## Requirements

### Requirement: Загрузка и валидация config.yaml

При старте движок ДОЛЖЕН читать конфигурационный файл YAML (по умолчанию ./config.yaml, путь — флагом --config). Отсутствие файла или невалидные значения (например, отрицательный порт, voting-секции неверных типов) ДОЛЖНЫ завершать процесс немедленно с кодом 1 и выводом ошибки в stderr. Структура секций ДОЛЖНА соответствовать docs/08 §1.2: server (ssl, host, port, threads_num, max_request_body_size, request_timeout_sec), storage (path, max_open_files, write_buffer_size, max_write_buffer_number, log_level), auth (mode NATIVE|OIDC + oidc), sse (heartbeat_interval_sec), metrics (enabled, path), mcp (protocol_version, supported_versions, tools_list_cache_ttl_ms), logging (level, format, async, async_queue_size, log_file), rate_limit (неактивен в On-Premises (Self-Hosted)). Ключ mcp.supported_versions ДОЛЖЕН быть списком непустых строк версий протокола; список ОБЯЗАН быть непустым и ДОЛЖЕН содержать значение mcp.protocol_version; невалидный или противоречивый список ДОЛЖЕН завершать процесс с кодом 1.

#### Scenario: Валидный конфиг поднимает сервер

- **WHEN** движок стартует с корректным config.yaml
- **THEN** все параметры применяются: слушающий адрес/порт, пути хранилища, интервал heartbeat, TTL каталога tools/list

#### Scenario: Отсутствующий конфиг

- **WHEN** указанный файл конфигурации не существует
- **THEN** процесс завершается кодом 1 с сообщением об ошибке в stderr

#### Scenario: Невалидное значение

- **WHEN** в конфиге задан отрицательный порт
- **THEN** процесс завершается кодом 1 с описанием невалидного параметра в stderr

#### Scenario: supported_versions без protocol_version отклоняется

- **WHEN** в конфиге mcp.supported_versions = ["2025-06-18"], а mcp.protocol_version = "2026-07-28"
- **THEN** процесс завершается кодом 1 с описанием противоречия в stderr

#### Scenario: Пустой supported_versions отклоняется

- **WHEN** в конфиге mcp.supported_versions = []
- **THEN** процесс завершается кодом 1 с описанием невалидного параметра в stderr

#### Scenario: Переопределение списка окружением

- **WHEN** запущено с VOTERPOOL_MCP_SUPPORTED_VERSIONS="2026-07-28,2025-06-18"
- **THEN** сервер согласовывает версии по списку из окружения независимо от значения в config.yaml

### Requirement: Переопределение переменными окружения

Любой параметр конфига ДОЛЖЕН переопределяться переменной окружения формата VOTERPOOL_{SECTION}_{KEY}; окружение имеет приоритет над config.yaml.

#### Scenario: Порт из окружения

- **WHEN** запущено с VOTERPOOL_SERVER_PORT=8081 при порте 8080 в config.yaml
- **THEN** сервер слушает порт 8081

### Requirement: CLI-флаги

Бинарник ДОЛЖЕН принимать флаги: `--config <path>`, `--port <port>`, `--db-path <path>`, `--log-level <level>`, `--daemon` (фоновый запуск через fork). Приоритет: CLI-флаги выше переменных окружения и файла.

#### Scenario: Флаги перекрывают файл и окружение

- **WHEN** запущено `./voterpool --config a.yaml --port 9090 --db-path /tmp/db --log-level debug`
- **THEN** сервер использует порт 9090, хранилище /tmp/db и уровень логирования debug независимо от файла и окружения

#### Scenario: Daemon-режим

- **WHEN** запущено `./voterpool --daemon`
- **THEN** процесс уходит в фон; сервер доступен на настроенном порту

### Requirement: Проверка доступа к хранилищу

При старте движок ОБЯЗАН проверить права чтения и записи директории storage.path; при их отсутствии — критическая запись лога и завершение процесса (код 1).

#### Scenario: Нет прав на директорию данных

- **WHEN** движок стартует с storage.path без прав записи
- **THEN** процесс завершается кодом 1 после критического лог-сообщения

### Requirement: Применение транспортных параметров

Сервер ДОЛЖЕН применять параметры секции server к HTTP/SSE-листенеру: ssl.enabled=true ДОЛЖЕН поднимать TLS-листенер (HTTPS для /mcp и SSE) с сертификатом cert_path и ключом key_path; ssl.enabled=false — plaintext HTTP. При ssl.enabled=true отсутствующий, нечитаемый или невалидный файл сертификата или ключа ДОЛЖЕН приводить к фатальному отказу старта: критическая запись в лог и завершение процесса с кодом 1 до открытия сетевых соединений. max_request_body_size ДОЛЖЕН ограничивать размер тела HTTP-запроса: запрос с телом больше лимита ДОЛЖЕН отклоняться сервером без обработки MCP-сообщения. request_timeout_sec ДОЛЖЕН применяться как таймаут простоя соединения; активные SSE-подписки НЕ ДОЛЖНЫ закрываться по этому таймауту (heartbeat поддерживает их живыми). Параметры применяются одинаково при задании через config.yaml, переменные окружения и CLI-флаги.

#### Scenario: TLS-листенер принимает HTTPS

- **WHEN** движок стартует с ssl.enabled=true и валидными cert_path/key_path
- **THEN** HTTPS-запрос к /health проходит рукоположение TLS и отвечает 200, plaintext-клиент не получает ответа приложения

#### Scenario: Невалидный сертификат фатален

- **WHEN** ssl.enabled=true, а cert_path указывает на несуществующий файл
- **THEN** процесс завершается кодом 1 с критическим сообщением в логе, порт не открывается

#### Scenario: Превышение лимита тела запроса

- **WHEN** POST /mcp получает тело больше max_request_body_size
- **THEN** сервер отклоняет запрос без разбора JSON-RPC и без записи в бизнес-метриках инструментов

#### Scenario: SSE переживает таймаут простоя

- **WHEN** подписка /mcp/events открыта дольше request_timeout_sec при heartbeat_interval_sec < request_timeout_sec
- **THEN** соединение остаётся активным и продолжает получать кадры heartbeat

#### Scenario: Plaintext при выключенном SSL

- **WHEN** ssl.enabled=false (значение по умолчанию)
- **THEN** обычный HTTP-запрос к /health отвечает 200
