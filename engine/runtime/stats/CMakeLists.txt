add_library(RuntimePerformanceStatsCommand STATIC
    performance_stats_command_provider.cpp
)

target_include_directories(RuntimePerformanceStatsCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimePerformanceStatsCommand
    PUBLIC RuntimeCommand Render
)
