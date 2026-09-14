add_executable(
    MathUnitTest
    math_test.cpp
)

target_link_libraries(MathUnitTest PRIVATE Math KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(MathUnitTest)
