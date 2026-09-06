add_executable(DatabaseUnitTest
    database_test.cpp
)

target_link_libraries(DatabaseUnitTest PRIVATE Core Database KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(DatabaseUnitTest)
