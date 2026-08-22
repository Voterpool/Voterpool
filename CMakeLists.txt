cmake_minimum_required(VERSION 3.20)
project(voterpool CXX)

if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VOTERPOOL_BUILD_TESTS "Build tests" OFF)
option(VOTERPOOL_JEMALLOC "Override global new/delete with jemalloc" ON)

find_package(Drogon CONFIG REQUIRED)
find_package(RocksDB CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_path(JSONCPP_INCLUDE_DIR json/json.h PATH_SUFFIXES jsoncpp)
find_library(JSONCPP_LIB jsoncpp)
message(STATUS "jsoncpp: ${JSONCPP_INCLUDE_DIR} ${JSONCPP_LIB}")
if(VOTERPOOL_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

find_library(JEMALLOC_LIB jemalloc)

file(GLOB_RECURSE VOTERPOOL_SOURCES "src/*.cpp")
list(FILTER VOTERPOOL_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(voterpool_core STATIC ${VOTERPOOL_SOURCES})
target_include_directories(voterpool_core PUBLIC include third_party/concurrentqueue ${JSONCPP_INCLUDE_DIR})
target_link_libraries(voterpool_core PUBLIC
    Drogon::Drogon
    RocksDB::rocksdb
    simdjson::simdjson
    spdlog::spdlog
    yaml-cpp
    OpenSSL::Crypto
    ${JSONCPP_LIB}
)
if(JEMALLOC_LIB AND VOTERPOOL_JEMALLOC)
    target_link_libraries(voterpool_core PUBLIC ${JEMALLOC_LIB})
    target_compile_definitions(voterpool_core PUBLIC VOTERPOOL_WITH_JEMALLOC=1)
endif()
target_compile_options(voterpool_core PRIVATE -Wall -Wextra)

add_executable(voterpool src/main.cpp)
target_link_libraries(voterpool PRIVATE voterpool_core)

install(TARGETS voterpool DESTINATION bin)
install(FILES config/default.yaml DESTINATION etc/voterpool)

if(VOTERPOOL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
