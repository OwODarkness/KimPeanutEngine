target_sources(EditorUILib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_layout_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_settings_window_component.cpp
)

# The feature-specific Settings UI lives with RuntimeSettings, but this
# editor-only subdirectory contributes sources to EditorUILib, never RuntimeSettings.
target_include_directories(EditorUILib PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime/settings/editor)
add_subdirectory(
    ${CMAKE_SOURCE_DIR}/engine/runtime/settings/editor
    ${CMAKE_CURRENT_BINARY_DIR}/runtime_settings_editor)

add_library(SettingsPropertyFields STATIC
    settings_property_fields_view.cpp
)
target_include_directories(SettingsPropertyFields PUBLIC
    ${CMAKE_SOURCE_DIR}/engine/editor/settings
)
target_link_libraries(SettingsPropertyFields PUBLIC UIWidgets RuntimeSettings)
