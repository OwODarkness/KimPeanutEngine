add_library(Asset STATIC 
asset_manager.cpp
assimp_model_loader.cpp
model.cpp
stb_image_loader.cpp
shader_program.cpp
shader_program_loader.cpp
miniaudio_audio_loader.cpp
)
target_include_directories(Asset PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
${CMAKE_CURRENT_SOURCE_DIR}/../..

)
target_link_libraries(Asset PUBLIC Data Base PRIVATE Log magic_enum stb_image nlohmann assimp miniaudio)