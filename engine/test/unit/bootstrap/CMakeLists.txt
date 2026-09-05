add_executable(
    BootstrapTest
    bootstrap_test.cpp
)

target_link_libraries(
    BootstrapTest
    PRIVATE
    Bootstrap
    Asset
    Config
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(BootstrapTest)
