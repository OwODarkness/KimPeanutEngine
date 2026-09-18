add_executable(
    LogUnitTest
    logger_test.cpp
)

target_link_libraries(LogUnitTest PRIVATE Log KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(LogUnitTest PROPERTIES LABELS "log")
