add_executable(TTSUnitTest
    tts_system_test.cpp
    gpt_sovits_tts_test.cpp
    tts_editor_settings_test.cpp
    "${CMAKE_SOURCE_DIR}/engine/module/tts/editor/tts_editor_settings.cpp")
target_link_libraries(TTSUnitTest PRIVATE TTS KP::GoogleTest KP::HttpLib KP::NlohmannJson)
copy_runtime_dependencies(TTSUnitTest)

include(GoogleTest)
gtest_discover_tests(TTSUnitTest PROPERTIES LABELS "tts")

add_executable(TtsEditorControllerTest tts_editor_controller_test.cpp)
target_link_libraries(TtsEditorControllerTest PRIVATE
    TtsEditorMode KP::GoogleTest)
copy_runtime_dependencies(TtsEditorControllerTest)
gtest_discover_tests(TtsEditorControllerTest PROPERTIES LABELS "tts")

add_executable(TtsEditorLibraryTest tts_editor_library_test.cpp)
target_link_libraries(TtsEditorLibraryTest PRIVATE TtsEditorMode KP::GoogleTest)
copy_runtime_dependencies(TtsEditorLibraryTest)
gtest_discover_tests(TtsEditorLibraryTest PROPERTIES LABELS "tts")
