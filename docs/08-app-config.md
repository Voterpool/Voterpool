OpenSpec: Конфигурация приложения (config.yaml)
Версия: 1.0.0
Статус: Draft

# 1. Конфигурация приложения
## 1.1. Формат и загрузка конфигурации
Формат: YAML 1.2.
Загрузка: При старте бинарника (./ace-engine --config=/path/to/config.yaml), C++ парсер (например, yaml-cpp) читает файл в статическую структуру конфигурации.
Валидация: Если файл отсутствует или содержит невалидные значения (например, отрицательный порт), процесс немедленно завершается с кодом 1 и выводит ошибку в stderr.
Environment Variables: Поддерживается переопределение любых параметров через переменные окружения (полезно для Docker/K8s). Формат: ACE_{SECTION}_{KEY} (например, ACE_SERVER_PORT=8081).

## 1.2. Структура config.yaml
Ниже представлен полный пример конфигурационного файла с комментариями.

yaml

# ==========================================
# ACE Engine Configuration
# ==========================================

server:
  ssl:
  # Включить TLS/SSL для HTTP и SSE (Enterprise)
  enabled: false
  cert_path: "/etc/ssl/certs/ace.crt"
  key_path: "/etc/ssl/private/ace.key"
  # IP адрес для привязки. "0.0.0.0" для всех интерфейсов.
  host: "0.0.0.0"
  # Порт для HTTP/MCP и SSE соединений.
  port: 8080
  # Количество рабочих потоков Drogon (IO-воркеры).
  # 0 = автоопределение (равно количеству логических ядер CPU).
  threads_num: 0
  # Максимальный размер тела HTTP-запроса (для MCP JSON-RPC). 10 MB.
  max_request_body_size: 10485760
  # Таймаут ожидания запроса (секунды). Для SSE-соединений игнорируется.
  request_timeout_sec: 30

storage:
  # Путь к директории RocksDB. Должна быть доступна для записи.
  path: "./data/ace_db"
  # Максимальное количество открытых файлов RocksDB (-1 означает безлимит).
  max_open_files: -1
  # Размер одного MemTable в памяти перед сбросом на диск (64 MB).
  write_buffer_size: 67108864
  # Максимальное количество MemTable в памяти одновременно.
  max_write_buffer_number: 3
  # Уровень логирования RocksDB (ERROR, WARN, INFO, DEBUG).
  log_level: "WARN"

auth:
  # Режим авторизации: "NATIVE" (встроенные токены) или "OIDC" (Enterprise JWT).
  mode: "NATIVE"
  
  # Настройки для OIDC mode (игнорируются, если mode: NATIVE)
  oidc:
    # URL публичных ключей СУДИР (Keycloak, Okta) для проверки подписи JWT.
    jwks_url: "https://keycloak.corp.com/realms/internal/protocol/openid-connect/certs"
    # Эмитент (iss), который должен присутствовать в JWT.
    issuer: "https://keycloak.corp.com/realms/internal"
    # Время кэширования валидных JWTS в памяти C++ (секунды).
    cache_ttl_sec: 300

sse:
  # Интервал отправки keep-alive комментариев (секунды).
  # Защищает соединения от обрыва прокси-серверами (Nginx, CloudFlare).
  heartbeat_interval_sec: 15

logging:
  # Уровень логирования приложения (trace, debug, info, warn, error).
  level: "info"
  # Формат вывода логов spdlog.
  format: "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v"
  # Включить асинхронное логирование (очередь сообщений).
  async: true
  # Размер очереди для асинхронного логгера (должна быть степенью двойки).
  async_queue_size: 8192
  # Путь к файлу логов. Если пусто, логи идут в stdout.
  log_file: ""

rate_limit:
  # Включить лимитирование запросов (RPS) на одного агента.
  enabled: false
  # Лимит запросов в секунду на один agent_id.
  rps_per_agent: 50
  # Лимит запросов в секунду на одну организацию (суммарно от всех агентов).
  rps_per_org: 500

## 1.3. Детали конфигурации (C++ Implementation)
## 1.3.1. Интеграция с Drogon
Конфигурация server напрямую транслируется в настройки Drogon перед вызовом app().run():

cpp

drogon::app()
    .setListenAddress(config.server.host)
    .setPort(config.server.port)
    .setThreadNum(config.server.threads_num);

## 1.3.2. Опции запуска (CLI Flags)
В дополнение к config.yaml, бинарник принимает флаги командной строки для быстрого оверрайда (удобно для CI/CD и тестов):

--config <path>: Указать путь к конфигурационному файлу (по умолчанию ./config.yaml).
--port <port>: Переопределить server.port.
--db-path <path>: Переопределить storage.path.
--log-level <level>: Переопределить logging.level.
--daemon: Запустить процесс в фоновом режиме (fork).

## 1.3.3. Безопасность (Permissions)
При запуске C++ движок проверяет права доступа к директории storage.path. Если процесс не имеет прав на чтение и запись в эту директорию, RocksDB выдаст ошибку инициализации. Спецификация требует:

Проверить access(storage.path.c_str(), W_OK | R_OK).
При отсутствии прав — spdlog::critical и завершение процесса (exit(1)).
