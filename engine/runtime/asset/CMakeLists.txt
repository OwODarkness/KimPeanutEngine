add_library(Asset STATIC 
asset_manager.cpp
assimp_model_loader.cpp
model.cpp
stb_image_loader.cpp
shader_meta.cpp
shader_loader.cpp
)
target_include_directories(Asset PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(Asset PUBLIC Data PRIVATE Log)