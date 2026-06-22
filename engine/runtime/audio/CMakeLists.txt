add_library(Audio STATIC
    miniaudio_audio_system.cpp
    audio_player.cpp
)

target_include_directories(Audio PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Audio PRIVATE ThirdPartyLib Log)