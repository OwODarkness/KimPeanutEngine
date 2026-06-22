add_library(TTS STATIC gpt_sovits_tts.cpp)

target_include_directories(TTS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(TTS PUBLIC Core PRIVATE ThirdPartyLib Log Audio)