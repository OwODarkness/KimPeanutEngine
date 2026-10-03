add_library(TtsEditorMode STATIC
    tts_editor_settings.cpp
    tts_editor_wav.cpp
    tts_editor_library.cpp
    tts_editor_controller.cpp
    tts_editor_editor.cpp
    tts_editor_host.cpp)
target_include_directories(TtsEditorMode PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(TtsEditorMode
    PUBLIC TTS RuntimeLib
    PRIVATE EditorUILib KP::ImGui KP::NlohmannJson)
