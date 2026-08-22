OpenSpec: Структура проекта (CMake / C++)
Версия: 1.0.0
Статус: Draft

# 1. Структура проекта
## 1.1. Дерево директорий
Проект следует классической архитектуре с разделением на заголовочные файлы (include/) и файлы реализации (src/). Логика разделена на независимые модули (слои), что соответствует принципам Clean Architecture.

text

voterpool/
├── CMakeLists.txt              # Корневой файл сборки CMake
├── vcpkg.json                  # Манифест зависимостей vcpkg
├── config/
│   └── default.yaml            # Конфигурация по умолчанию
├── include/                    # Публичные и внутренние заголовочные файлы
│   ├── core/                   # Базовые примитивы (логгер, конфиг, аллокаторы)
│   │   ├── Config.h
│   │   ├── Logger.h
│   │   ├── Metrics.h           # Реестр метрик Prometheus (docs/11)
│   │   └── Memory.h            # Инициализация jemalloc
│   ├── domain/                 # Доменные сущности (POCO/DTO)
│   │   ├── Agent.h
│   │   ├── Organization.h
│   │   ├── Membership.h
│   │   ├── Proposal.h
│   │   └── Vote.h
│   ├── storage/                # Слой персистентности (Обертки над RocksDB)
│   │   ├── Database.h         # Интерфейс IDatabase
│   │   ├── RocksDBWrapper.h
│   │   ├── SchemaVersion.h    # VOTERPOOL_SCHEMA_VERSION + раннер миграций (docs/12)
│   │   └── repositories/      # Repository Pattern
│   │       ├── AgentRepository.h
│   │       ├── OrgRepository.h
│   │       ├── ProposalRepository.h
│   │       └── VoteRepository.h
│   ├── consensus/              # Движок принятия решений
│   │   ├── IConsensusModel.h  # Интерфейс стратегии
│   │   ├── MajorityModel.h
│   │   ├── QuorumModel.h
│   │   ├── ConsentModel.h
│   │   ├── ConsensusEngine.h  # Оркестратор
│   └── ProposalLock.h     # Per-proposal mutex registry (контроль конкурентности, docs/01 §5)
│   ├── server/                 # Сетевой слой (Drogon)
│   │   ├── AuthMiddleware.h   # Проверка токенов (Native/OIDC)
│   │   ├── SseHub.h           # Менеджер SSE-соединений
│   │   └── Workers.h          # Фоновые потоки (TTL, SSE Dispatcher)
│   └── mcp/                    # Слой MCP (Model Context Protocol)
│       ├── McpHandler.h        # Маршрутизатор JSON-RPC
│       ├── JsonRpcError.h      # Стандартизированные ошибки
│       └── tools/              # Реализация MCP Tools
│           ├── RegisterAgent.h
│           ├── CreateOrg.h
│           ├── JoinOrg.h
│           ├── CreateProposal.h
│           ├── CastVote.h
│           └── GetProposals.h
├── src/                        # Файлы реализации (.cpp), зеркалирующие include/
│   ├── core/
│   ├── domain/
│   ├── storage/
│   ├── consensus/
│   ├── server/
│   ├── mcp/
│   └── main.cpp                # Точка входа, инициализация подсистем
└── tests/                      # Тесты (GoogleTest) — см. docs/10
    ├── CMakeLists.txt
    ├── common/                 # Помощники: TempDbFixture, MockClock,
    │   │                       #   TestServer, SseClient, RandomPort
    ├── unit/                   # Чистая логика без I/O
    │   ├── test_consensus_majority.cpp
    │   ├── test_consensus_quorum.cpp
    │   ├── test_consensus_consent.cpp
    │   ├── test_jsonrpc_errors.cpp
    │   ├── test_params_validation.cpp
    │   └── test_keys.cpp
    ├── integration/            # Реальный RocksDB во временной директории
    │   ├── test_storage_repos.cpp
    │   ├── test_cast_vote_tx.cpp
    │   ├── test_concurrency.cpp
    │   ├── test_actions.cpp
    │   ├── test_discovery_index.cpp
    │   ├── test_ttl_worker.cpp
    │   ├── test_recovery.cpp
    │   ├── test_auth_native.cpp
    │   ├── test_migration.cpp
    │   ├── test_checkpoint_restore.cpp
    │   └── test_degraded_mode.cpp
    └── e2e/                    # Реальный сервер на localhost + HTTP/SSE
        ├── test_protocol.cpp
        ├── test_mcp_flow.cpp
        ├── test_sse_events.cpp
        ├── test_errors_http.cpp
        └── test_shutdown.cpp

## 1.2. Управление зависимостями (vcpkg.json)
Все внешние библиотеки управляются через манифест vcpkg.json (Manifest Mode). Это гарантирует, что любой разработчик или CI/CD соберет проект с одинаковыми версиями зависимостей.

json

{
  "name": "voterpool",
  "version-string": "1.0.0",
  "dependencies": [
    "drogon",
    "rocksdb",
    "simdjson",
    "spdlog",
    "jemalloc",
    "yaml-cpp",
    "gtest",
    {
      "name": "jwt-cpp",
      "features": ["openssl"]
    }
  ]
}
Сборка RocksDB с vcpkg по умолчанию компилирует его как статическую библиотеку.

Зависимость jwt-cpp требуется только для Enterprise OIDC-режима (отложен, docs/03 §1.1). В MVP-native её можно исключить из vcpkg.json; интерфейс IAuthProvider (docs/03) резервирует место под OIDCAuthProvider без обязательной линковки jwt-cpp.

## 1.3. Конфигурация CMake (CMakeLists.txt)
Корневой CMakeLists.txt настроен на production-сборку: статическая линковка runtime-библиотек C/C++ и интеграция jemalloc.

cmake

cmake_minimum_required(VERSION 3.20)
project(voterpool CXX)

# Включение манифеста vcpkg
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Опции сборки
option(VOTERPOOL_BUILD_TESTS "Build unit tests" OFF)

# Поиск пакетов
find_package(Drogon CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(jemalloc CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

# Сбор исходников (рекурсивный сбор всех .cpp)
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Целевой бинарник
add_executable(voterpool ${SOURCES})

# Линковка библиотек
target_link_libraries(voterpool PRIVATE
    Drogon::Drogon
    RocksDB::rocksdb
    simdjson::simdjson
    spdlog::spdlog
    jemalloc::jemalloc
    yaml-cpp
)

# Включение статической линковки runtime (для автономного бинарника)
if(MSVC)
    set_property(TARGET voterpool PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(voterpool PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Интеграция jemalloc (переопределение глобальных new/delete)
target_compile_definitions(voterpool PRIVATE JEMALLOC_NO_DEMANGLE)

# Тесты
if(VOTERPOOL_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()

# Установка (Install)
install(TARGETS voterpool DESTINATION bin)
install(FILES config/default.yaml DESTINATION etc/voterpool)
8.4. Инициализация в main.cpp
Точка входа собирает все компоненты воедино (Dependency Injection) и запускает сервер.

Псевдокод main.cpp:

cpp

#include "core/Config.h"
#include "core/Logger.h"
#include "storage/RocksDBWrapper.h"
#include "server/Workers.h"
#include "server/SseHub.h"
#include "mcp/McpHandler.h"

#include <drogon/drogon.h>

int main(int argc, char** argv) {
    // 1. Парсинг CLI и загрузка config.yaml
    auto config = voterpool::Config::load(argc, argv);
    
    // 2. Инициализация логгера (spdlog async)
    voterpool::Logger::init(config.logging);
    
    // 3. Инициализация хранилища (RocksDB)
    auto db = std::make_shared<voterpool::RocksDBWrapper>(config.storage);
    
    // 4. Инициализация менеджера SSE и Воркеров
    auto sse_hub = std::make_shared<voterpool::SseHub>();
    auto workers = std::make_shared<voterpool::Workers>(db, sse_hub, config);
    workers->start(); // Запуск std::jthread для TTL и SSE dispatcher
    
    // 5. Инициализация бизнес-логики и MCP хендлера
    auto mcp_handler = std::make_shared<voterpool::McpHandler>(db, sse_hub, config);
    
    // 6. Настройка Drogon
    drogon::app()
        .setLogLevel(drogon::LogLevel::WARN)
        .addListener(config.server.host, config.server.port)
        .setThreadNum(config.server.threads_num);
        
    // Регистрация Middleware (Авторизация). Мягкий режим (docs/03 §1.2): невалидный токен
   // блокируется сразу; ОТСУТСТВИЕ заголовка проходит дальше — McpHandler сам требует
   // AgentContext для всех инструментов, кроме register_agent. GET /health анонимен.
   auto auth_middleware = std::make_shared<voterpool::AuthMiddleware>(config.auth, db);
   drogon::app().registerPreHandlingAdvice(auth_middleware);

    // Регистрация маршрутов
    drogon::app().registerHandler("/mcp", 
        [&mcp_handler](const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            mcp_handler->handle(req, std::move(callback));
        }, {drogon::Post});
        
    drogon::app().registerHandler("/mcp/events", 
        [&sse_hub](const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            sse_hub->handleSubscription(req, std::move(callback));
        }, {drogon::Get});

    // Обработка SIGTERM / SIGINT для грациозной остановки
    signal(SIGTERM, [](int) { 
        spdlog::info("Received SIGTERM. Shutting down gracefully...");
        drogon::app().getLoop()->queueInLoop([]{ drogon::app().quit(); });
    });

    // 7. Запуск сервера (блокирующий вызов)
    spdlog::info("Voterpool Engine started on port {}", config.server.port);
    drogon::app().run();

    // 8. Очистка после остановки Drogon
    workers->stop();
    db->close();
    
    return 0;
}