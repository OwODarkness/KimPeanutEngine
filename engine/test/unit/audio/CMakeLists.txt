add_executable(
    AudioUnitTest
    audio_decode_test.cpp
    seekable_audio_player_test.cpp
    )

add_executable(
    AudioDeviceSmoke
    audio_device_smoke.cpp
    )


target_link_libraries(
    AudioUnitTest
    PRIVATE
    Asset
    Audio
    KP::GoogleTest
)

target_link_libraries(
    AudioDeviceSmoke
    PRIVATE
    Audio
)

copy_runtime_dependencies(AudioUnitTest)

include(GoogleTest)
gtest_discover_tests(AudioUnitTest PROPERTIES LABELS "audio")
