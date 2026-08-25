## MODIFIED Requirements

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
