add_library(AudioPlayerModule STATIC
    ../../editor/ui/component/editor_audio_component.cpp
    audio_preview_widget.cpp
    audio_player_controller.cpp
    audio_player_editor.cpp
    audio_player_host.cpp)
target_include_directories(AudioPlayerModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerModule
    PUBLIC Audio RuntimeLib
    PRIVATE ImageIO EditorUILib KP::ImGui)
if(WIN32)
    target_link_libraries(AudioPlayerModule PRIVATE comdlg32 shell32 ole32)
endif()

add_library(AudioPlayerRegistration OBJECT audio_player_registration.cpp)
target_include_directories(AudioPlayerRegistration PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerRegistration PRIVATE AudioPlayerModule ModuleBootstrap)
