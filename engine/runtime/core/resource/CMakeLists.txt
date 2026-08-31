add_library(Resource STATIC 
    shader_compiler.cpp
    spirv_compiler.cpp
    preprocess_operation.cpp
    shader_cache.cpp
    shader_processor.cpp
    resource_pipeline.cpp
    environment_ibl_processor.cpp
    utility.cpp
)
target_include_directories(Resource PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(Resource PRIVATE Asset Log Data Base Config ThirdPartyLib magic_enum nlohmann)
