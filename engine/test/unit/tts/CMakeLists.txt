add_executable(TTSUnitTest tts_system_test.cpp)
target_link_libraries(TTSUnitTest PRIVATE TTS KP::GoogleTest)
copy_runtime_dependencies(TTSUnitTest)

include(GoogleTest)
gtest_discover_tests(TTSUnitTest PROPERTIES LABELS "tts")
