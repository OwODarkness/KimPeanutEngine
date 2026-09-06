add_library(Core INTERFACE)

add_subdirectory(base)
add_subdirectory(math)
add_subdirectory(spatial)
add_subdirectory(config)
add_subdirectory(log)
add_subdirectory(delegate)
add_subdirectory(data)
add_subdirectory(resource)
add_subdirectory(async)
add_subdirectory(database)

# Database is an optional persistence service. It must be requested by the
# archive/import targets explicitly rather than leaking through the common
# runtime Core umbrella.
target_link_libraries(Core INTERFACE Math Spatial Config Log Delegate Base Data Resource Async)
