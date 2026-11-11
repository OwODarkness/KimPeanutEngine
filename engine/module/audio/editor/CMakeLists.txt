add_library(AudioEditorWidgets STATIC audio_transport_controls.cpp)
target_include_directories(AudioEditorWidgets PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioEditorWidgets PUBLIC UIWidgets EditorIcons)

add_library(AudioPlayerEditorUI STATIC
    audio_player_editor.cpp
    audio_preview_widget.cpp)
target_include_directories(AudioPlayerEditorUI PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerEditorUI
    PUBLIC UIWidgets
    PRIVATE AudioPlayerController AudioEditorWidgets EditorWorkspaceLib EditorUiHostLib RuntimeLib)
