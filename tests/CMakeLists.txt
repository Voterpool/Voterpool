add_executable(voterpool_unit_tests
    unit/test_consensus_models.cpp
    unit/test_jsonrpc_errors.cpp
    unit/test_keys_bucket.cpp
    unit/test_params_validation.cpp
    unit/test_config.cpp
    unit/test_onboarding.cpp
    unit/test_proposal_lock.cpp
    unit/test_request_log.cpp
    unit/test_scaling_ports.cpp
    unit/test_org_name_registry.cpp
    unit/test_codec.cpp
    unit/test_metrics_exposition.cpp
)
target_link_libraries(voterpool_unit_tests PRIVATE voterpool_core GTest::gtest_main)

add_executable(voterpool_integration_tests
    integration/test_storage_repos.cpp
    integration/test_audit_log.cpp
    integration/test_cast_vote_tx.cpp
    integration/test_concurrency.cpp
    integration/test_actions.cpp
    integration/test_consent_flow.cpp
    integration/test_discovery_index.cpp
    integration/test_ttl_worker.cpp
    integration/test_recovery.cpp
    integration/test_auth_native.cpp
    integration/test_migration.cpp
    integration/test_bucket_migration.cpp
    integration/test_bucket_scans.cpp
    integration/test_checkpoint_restore.cpp
    integration/test_degraded_mode.cpp
    integration/test_onboarding_flow.cpp
    integration/test_golden_regression.cpp
    integration/test_config_delta.cpp
    integration/test_metrics_semantics.cpp
)
target_link_libraries(voterpool_integration_tests PRIVATE voterpool_core GTest::gtest_main)

add_executable(voterpool_e2e_tests
    e2e/test_protocol.cpp
    e2e/test_mcp_flow.cpp
    e2e/test_sse_events.cpp
    e2e/test_errors_http.cpp
    e2e/test_onboarding_http.cpp
    e2e/test_transport.cpp
    e2e/E2eEnv.cpp
    common/HttpUtil.cpp
    common/SseClient.cpp
)
target_link_libraries(voterpool_e2e_tests PRIVATE voterpool_core GTest::gtest_main)
add_dependencies(voterpool_e2e_tests voterpool)
target_compile_definitions(voterpool_e2e_tests PRIVATE VOTERPOOL_BIN="$<TARGET_FILE:voterpool>")

add_executable(voterpool_e2e_shutdown
    e2e/test_shutdown.cpp
    common/HttpUtil.cpp
    common/SseClient.cpp
)
target_link_libraries(voterpool_e2e_shutdown PRIVATE voterpool_core GTest::gtest_main)
add_dependencies(voterpool_e2e_shutdown voterpool)
target_compile_definitions(voterpool_e2e_shutdown PRIVATE VOTERPOOL_BIN="$<TARGET_FILE:voterpool>")

include(GoogleTest)
gtest_discover_tests(voterpool_unit_tests DISCOVERY_TIMEOUT 60)
gtest_discover_tests(voterpool_integration_tests DISCOVERY_TIMEOUT 60 TEST_PREFIX "int.")
gtest_discover_tests(voterpool_e2e_tests DISCOVERY_TIMEOUT 120 TEST_PREFIX "e2e.")
gtest_discover_tests(voterpool_e2e_shutdown DISCOVERY_TIMEOUT 60 TEST_PREFIX "e2e.shutdown.")

target_include_directories(voterpool_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(voterpool_unit_tests PRIVATE
    VOTERPOOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    VOTERPOOL_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(voterpool_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(voterpool_integration_tests PRIVATE
    VOTERPOOL_GOLDEN_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden_scenario.json"
    VOTERPOOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_include_directories(voterpool_e2e_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(voterpool_e2e_shutdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(voterpool_hot_path_bench EXCLUDE_FROM_ALL bench/hot_path_bench.cpp)
target_link_libraries(voterpool_hot_path_bench PRIVATE voterpool_core)

add_executable(voterpool_bench EXCLUDE_FROM_ALL bench/voterpool_bench.cpp)
target_link_libraries(voterpool_bench PRIVATE voterpool_core)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(
        NAME seam_isolation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/seams/isolation_guard.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
endif()
