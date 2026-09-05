add_executable(CommandRegistryTest
    command_registry_test.cpp
    command_parser_test.cpp
    command_execution_context_test.cpp
    command_local_transport_test.cpp
)

target_link_libraries(CommandRegistryTest PRIVATE RuntimeCommand KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(CommandRegistryTest)
