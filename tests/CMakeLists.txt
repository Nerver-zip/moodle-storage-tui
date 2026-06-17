add_executable(unit_tests
    test_moodle_client.cpp
    test_session_manager.cpp
    test_history_manager.cpp
    test_commands.cpp
    test_tui.cpp
)

target_link_libraries(unit_tests
    PRIVATE
    mstorage_core
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(unit_tests)
