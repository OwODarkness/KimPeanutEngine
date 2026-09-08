target_sources(EditorUILib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_extension_registry.cpp
)

add_subdirectory(component)
