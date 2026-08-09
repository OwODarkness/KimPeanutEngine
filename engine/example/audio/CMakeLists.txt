add_library(AudioExample STATIC
audio_example.cpp)

target_include_directories(AudioExample PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(AudioExample
PRIVATE
Audio
Asset
)


