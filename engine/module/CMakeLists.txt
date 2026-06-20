add_library(Module INTERFACE)
add_subdirectory(tts)
target_link_libraries(Module INTERFACE TTS)