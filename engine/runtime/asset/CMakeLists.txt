add_library(Asset STATIC 
asset_manager.cpp
asset_load_observation.cpp
assimp_model_loader.cpp
model.cpp
shader_program.cpp
shader_program_loader.cpp
material_loader.cpp
level_loader.cpp
miniaudio_audio_loader.cpp
)
target_include_directories(Asset PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
${CMAKE_CURRENT_SOURCE_DIR}/../..

)
target_link_libraries(Asset PUBLIC Data Base PRIVATE Config ImageIO Log KP::MagicEnum KP::NlohmannJson KP::Assimp KP::Miniaudio)
