add_executable(
    AudioUnitTest
    audio_decode_test.cpp
    )


target_link_libraries(
    AudioUnitTest
    PRIVATE
    Asset
    Audio
    KP::GoogleTest
)

copy_runtime_dependencies(AudioUnitTest)

include(GoogleTest)
gtest_discover_tests(AudioUnitTest)
