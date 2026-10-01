add_library(TTSExample STATIC
tts_example.cpp
)

target_include_directories(TTSExample PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(TTSExample
PRIVATE
TTS
)

add_executable(TTSServiceSmoke tts_service_smoke.cpp)
target_link_libraries(TTSServiceSmoke PRIVATE TTSExample)


