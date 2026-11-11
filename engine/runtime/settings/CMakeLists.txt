add_library(RuntimeSettings STATIC
    settings_registry.cpp
    settings_store.cpp
    runtime_settings_editor_access.cpp
)

target_include_directories(RuntimeSettings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimeSettings PUBLIC Reflection PRIVATE KP::NlohmannJson)
