add_executable(TTSUnitTest tts_system_test.cpp gpt_sovits_tts_test.cpp)
target_link_libraries(TTSUnitTest PRIVATE TTS KP::GoogleTest KP::HttpLib)
copy_runtime_dependencies(TTSUnitTest)

include(GoogleTest)
gtest_discover_tests(TTSUnitTest PROPERTIES LABELS "tts")
