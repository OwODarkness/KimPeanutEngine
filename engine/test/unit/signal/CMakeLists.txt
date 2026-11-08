add_executable(SignalUnitTest
    signal_unit_test.cpp
    "${CMAKE_SOURCE_DIR}/engine/module/signal/tools/signal_scope_session.cpp")

target_link_libraries(SignalUnitTest PRIVATE
    Signal KP::GoogleTest)
target_include_directories(SignalUnitTest PRIVATE "${CMAKE_SOURCE_DIR}/engine/module/signal/tools")

include(GoogleTest)
gtest_discover_tests(SignalUnitTest PROPERTIES LABELS "signal")
