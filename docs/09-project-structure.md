OpenSpec: Структура проекта (CMake / C++)
Версия: 1.0.0
Статус: Draft

# 1. Структура проекта
## 1.1. Дерево директорий
Проект следует классической архитектуре с разделением на заголовочные файлы (include/) и файлы реализации (src/). Логика разделена на независимые модули (слои), что соответствует принципам Clean Architecture.

text

ace-engine/
├── CMakeLists.txt              # Корневой файл сборки CMake
├── vcpkg.json                  # Манифест зависимостей vcpkg
├── config/
│   └── default.yaml            # Конфигурация по умолчанию
├── include/                    # Публичные и внутренние заголовочные файлы
│   ├── core/                   # Базовые примитивы (логгер, конфиг, аллокаторы)
│   │   ├── Config.h
│   │   ├── Logger.h
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
│   │   └── ConsensusEngine.h  # Оркестратор
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
└── tests/                      # Unit & Integration тесты (GoogleTest)
    ├── CMakeLists.txt
    ├── test_consensus.cpp
    └── test_storage.cpp

## 1.2. Управление зависимостями (vcpkg.json)
Все внешние библиотеки управляются через манифест vcpkg.json (Manifest Mode). Это гарантирует, что любой разработчик или CI/CD соберет проект с одинаковыми версиями зависимостей.

json

{
  "name": "ace-engine",
  "version-string": "1.0.0",
  "dependencies": [
    "drogon",
    "rocksdb",
    "simdjson",
    "spdlog",
    "jemalloc",
    "yaml-cpp",
    {
      "name": "jwt-cpp",
      "features": ["openssl"]
    }
  ]
}
Сборка RocksDB с vcpkg по умолчанию компилирует его как статическую библиотеку.

## 1.3. Конфигурация CMake (CMakeLists.txt)
Корневой CMakeLists.txt настроен на production-сборку: статическая линковка runtime-библиотек C/C++ и интеграция jemalloc.

cmake

cmake_minimum_required(VERSION 3.20)
project(ace-engine CXX)

# Включение манифеста vcpkg
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Опции сборки
option(ACE_BUILD_TESTS "Build unit tests" OFF)

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
add_executable(ace-engine ${SOURCES})

# Линковка библиотек
target_link_libraries(ace-engine PRIVATE
    Drogon::Drogon
    RocksDB::rocksdb
    simdjson::simdjson
    spdlog::spdlog
    jemalloc::jemalloc
    yaml-cpp
)

# Включение статической линковки runtime (для автономного бинарника)
if(MSVC)
    set_property(TARGET ace-engine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(ace-engine PRIVATE -static-libstdc++ -static-libgcc)
endif()

# Интеграция jemalloc (переопределение глобальных new/delete)
target_compile_definitions(ace-engine PRIVATE JEMALLOC_NO_DEMANGLE)

# Тесты
if(ACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Установка (Install)
install(TARGETS ace-engine DESTINATION bin)
install(FILES config/default.yaml DESTINATION etc/ace)
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
    auto config = ace::Config::load(argc, argv);
    
    // 2. Инициализация логгера (spdlog async)
    ace::Logger::init(config.logging);
    
    // 3. Инициализация хранилища (RocksDB)
    auto db = std::make_shared<ace::RocksDBWrapper>(config.storage);
    
    // 4. Инициализация менеджера SSE и Воркеров
    auto sse_hub = std::make_shared<ace::SseHub>();
    auto workers = std::make_shared<ace::Workers>(db, sse_hub, config);
    workers->start(); // Запуск std::jthread для TTL и SSE dispatcher
    
    // 5. Инициализация бизнес-логики и MCP хендлера
    auto mcp_handler = std::make_shared<ace::McpHandler>(db, sse_hub, config);
    
    // 6. Настройка Drogon
    drogon::app()
        .setLogLevel(drogon::LogLevel::WARN)
        .addListener(config.server.host, config.server.port)
        .setThreadNum(config.server.threads_num);
        
    // Регистрация Middleware (Авторизация)
   auto auth_middleware = std::make_shared<ace::AuthMiddleware>(config.auth, db);
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
    spdlog::info("ACE Engine started on port {}", config.server.port);
    drogon::app().run();

    // 8. Очистка после остановки Drogon
    workers->stop();
    db->close();
    
    return 0;
}