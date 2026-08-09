add_library(Audio STATIC
    audio_system.cpp
    miniaudio_audio_system.cpp
    audio_player.cpp
    buffer_audio_player.cpp
    audio_stream.cpp
    audio_stream_decoder.cpp
    stream_audio_player.cpp
)

target_include_directories(Audio PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Audio PRIVATE Data Log miniaudio Math) 