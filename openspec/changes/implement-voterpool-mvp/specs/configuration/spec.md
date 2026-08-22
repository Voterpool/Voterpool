## Purpose

Конфигурация приложения: загрузка config.yaml (YAML 1.2) с валидацией, переопределение любыми переменными окружения VOTERPOOL_{SECTION}_{KEY} и CLI-флагами, проверка прав доступа к директории хранилища, режим daemon (docs/08).

## ADDED Requirements

### Requirement: Загрузка и валидация config.yaml

При старте движок ДОЛЖЕН читать конфигурационный файл YAML (по умолчанию ./config.yaml, путь — флагом --config). Отсутствие файла или невалидные значения (например, отрицательный порт, voting-секции неверных типов) ДОЛЖНЫ завершать процесс немедленно с кодом 1 и выводом ошибки в stderr. Структура секций ДОЛЖНА соответствовать docs/08 §1.2: server (ssl, host, port, threads_num, max_request_body_size, request_timeout_sec), storage (path, max_open_files, write_buffer_size, max_write_buffer_number, log_level), auth (mode NATIVE|OIDC + oidc), sse (heartbeat_interval_sec), metrics (enabled, path), mcp (protocol_version, tools_list_cache_ttl_ms), logging (level, format, async, async_queue_size, log_file), rate_limit (неактивен в MVP).

#### Scenario: Валидный конфиг поднимает сервер

- **WHEN** движок стартует с корректным config.yaml
- **THEN** все параметры применяются: слушающий адрес/порт, пути хранилища, интервал heartbeat, TTL каталога tools/list

#### Scenario: Отсутствующий конфиг

- **WHEN** указанный файл конфигурации не существует
- **THEN** процесс завершается кодом 1 с сообщением об ошибке в stderr

#### Scenario: Невалидное значение

- **WHEN** в конфиге задан отрицательный порт
- **THEN** процесс завершается кодом 1 с описанием невалидного параметра в stderr

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
