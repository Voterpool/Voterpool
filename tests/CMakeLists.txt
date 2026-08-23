add_executable(voterpool_unit_tests
    unit/test_consensus_models.cpp
    unit/test_jsonrpc_errors.cpp
    unit/test_keys.cpp
    unit/test_params_validation.cpp
    unit/test_config.cpp
    unit/test_onboarding.cpp
)
target_link_libraries(voterpool_unit_tests PRIVATE voterpool_core GTest::gtest_main)

add_executable(voterpool_integration_tests
    integration/test_storage_repos.cpp
    integration/test_cast_vote_tx.cpp
    integration/test_concurrency.cpp
    integration/test_actions.cpp
    integration/test_discovery_index.cpp
    integration/test_ttl_worker.cpp
    integration/test_recovery.cpp
    integration/test_auth_native.cpp
    integration/test_migration.cpp
    integration/test_checkpoint_restore.cpp
    integration/test_degraded_mode.cpp
    integration/test_onboarding_flow.cpp
)
target_link_libraries(voterpool_integration_tests PRIVATE voterpool_core GTest::gtest_main)

add_executable(voterpool_e2e_tests
    e2e/test_protocol.cpp
    e2e/test_mcp_flow.cpp
    e2e/test_sse_events.cpp
    e2e/test_errors_http.cpp
    e2e/test_onboarding_http.cpp
    e2e/E2eEnv.cpp
    common/HttpUtil.cpp
    common/SseClient.cpp
)
target_link_libraries(voterpool_e2e_tests PRIVATE voterpool_core GTest::gtest_main)

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

target_include_directories(voterpool_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(voterpool_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(voterpool_e2e_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(voterpool_e2e_shutdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
