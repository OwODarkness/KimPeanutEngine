add_executable(EnTTUnitTest
    entt_integration_test.cpp
)

target_link_libraries(EnTTUnitTest PRIVATE
    EnTT::EnTT
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(EnTTUnitTest)
