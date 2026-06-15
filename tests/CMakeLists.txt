add_executable(unit_tests
    test_moodle_client.cpp
    test_session_manager.cpp
    test_history_manager.cpp
    test_commands.cpp
    test_tui.cpp
)

target_link_libraries(unit_tests
    GTest::gtest_main
    GTest::gmock
    cpr::cpr
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ftxui::screen
    ftxui::dom
    ftxui::component
    ${SQLite3_LIBRARIES}
    ${libsecret_LIBRARIES}
)
target_include_directories(unit_tests PRIVATE ${SQLite3_INCLUDE_DIRS} ${libsecret_INCLUDE_DIRS})

include(GoogleTest)
gtest_discover_tests(unit_tests)
