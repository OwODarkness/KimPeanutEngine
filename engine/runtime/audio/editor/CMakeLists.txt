add_library(AudioPlayerMode STATIC
    "${CMAKE_SOURCE_DIR}/engine/editor/ui/component/editor_audio_component.cpp"
    audio_preview_widget.cpp
    audio_player_controller.cpp
    audio_player_editor.cpp
    audio_player_host.cpp)
target_include_directories(AudioPlayerMode PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerMode
    PUBLIC Audio RuntimeLib
    PRIVATE ImageIO EditorUILib KP::ImGui)
if(WIN32)
    target_link_libraries(AudioPlayerMode PRIVATE comdlg32 shell32 ole32)
endif()
