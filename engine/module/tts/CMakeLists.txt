add_library(TTS STATIC gpt_sovits_tts.cpp tts_system.cpp)

target_include_directories(TTS PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}/..
${CMAKE_CURRENT_SOURCE_DIR}/../..

)
target_link_libraries(TTS PUBLIC Core Audio PRIVATE Data Log KP::HttpLib Asset KP::NlohmannJson KP::Miniaudio)
