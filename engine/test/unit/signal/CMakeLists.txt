add_executable(SignalUnitTest
    signal_unit_test.cpp)

target_link_libraries(SignalUnitTest PRIVATE
    Signal KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(SignalUnitTest PROPERTIES LABELS "signal")
