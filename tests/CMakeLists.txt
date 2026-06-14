add_executable(unit_tests
    test_moodle_client.cpp
    test_session_manager.cpp
    test_history_manager.cpp
    test_commands.cpp
)

target_link_libraries(unit_tests
    GTest::gtest_main
    GTest::gmock
    cpr::cpr
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    SQLite3::SQLite3
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
