# This directory is included from engine/editor/settings. Its sources are
# compiled into the Editor-only target and are not part of RuntimeSettings.
target_sources(EditorUILib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_settings_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settings_editor_view.cpp
)
