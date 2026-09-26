add_library(RuntimeGameplayCommand STATIC
    gameplay_command_provider.cpp
)

target_include_directories(RuntimeGameplayCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimeGameplayCommand PUBLIC RuntimeCommand Gameplay)
