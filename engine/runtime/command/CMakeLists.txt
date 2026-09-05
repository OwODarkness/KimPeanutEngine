add_library(RuntimeCommand STATIC
    command_agent_endpoint.cpp
    command_local_transport.cpp
    command_parser.cpp
    command_registry.cpp
)

target_include_directories(RuntimeCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimeCommand
    PUBLIC Core
    PRIVATE KP::NlohmannJson ws2_32
)

add_executable(KimPeanutCommand
    command_headless_main.cpp
)

target_link_libraries(KimPeanutCommand PRIVATE RuntimeCommand)
