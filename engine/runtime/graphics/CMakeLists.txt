cmake_minimum_required(VERSION 3.5.0)

option(USE_OPENGL "Use OpenGL backend" ON)
option(USE_VULKAN "Use Vulkan backend" ON)

add_library(Graphics STATIC
    backend/common/render_backend.cpp
    backend/opengl/opengl_backend.cpp
    backend/vulkan/vulkan_backend.cpp
    tool/spirv_compiler.cpp    
)

target_include_directories(Graphics 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tool
)