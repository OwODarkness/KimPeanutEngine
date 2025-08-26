add_library(Core INTERFACE)

add_subdirectory(math)
add_subdirectory(config)
add_subdirectory(log)
add_subdirectory(delegate)

target_link_libraries(Core INTERFACE Math Config Log Delegate)