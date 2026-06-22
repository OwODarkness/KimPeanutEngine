cmake_minimum_required(VERSION 3.5.0)

option(USE_OPENGL "Use OpenGL backend" ON)
option(USE_VULKAN "Use Vulkan backend" ON)

add_library(Graphics STATIC
    backend/common/render_backend.cpp
    backend/common/shader_manager.cpp
    backend/common/texture_manager.cpp
    backend/common/sampler_manager.cpp
    backend/common/mesh_manager.cpp
    
    backend/opengl/opengl_backend.cpp
    backend/opengl/opengl_texture.cpp
    backend/opengl/opengl_shader.cpp
    backend/opengl/opengl_sampler.cpp
    backend/opengl/opengl_mesh.cpp
    backend/opengl/opengl_pipeline.cpp
    backend/opengl/opengl_descriptorset.cpp

    backend/vulkan/vulkan_backend.cpp
    backend/vulkan/vulkan_texture.cpp
    backend/vulkan/vulkan_shader.cpp
    backend/vulkan/vulkan_buffer_manager.cpp
    backend/vulkan/vulkan_memory_linear_allocator.cpp
    backend/vulkan/vulkan_memory_dedicated_allocator.cpp
    backend/vulkan/vulkan_memory_pool_allocator.cpp
    backend/vulkan/vulkan_pipeline_manager.cpp
    backend/vulkan/vulkan_image_memory_manager.cpp
    backend/vulkan/vulkan_sampler.cpp
    backend/vulkan/vulkan_mesh.cpp
    
    tool/shader_loader.cpp
    )

target_include_directories(Graphics 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tool/..
)

target_link_libraries(Graphics 
    PUBLIC Core 
    PRIVATE Asset)

find_program(GLSLC glslc
    HINTS
        "${CMAKE_SOURCE_DIR}/third_party/vulkan/bin"
)

if(NOT GLSLC AND DEFINED ENV{VULKAN_SDK})
    find_program(GLSLC glslc
        HINTS
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/bin"
    )
endif()

if(NOT GLSLC)
    find_program(GLSLC glslc) # search in PATH
endif()

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Please install Vulkan SDK or put glslc in third_party/vulkan/bin")
else()
    message(STATUS "Found glslc: ${GLSLC}, ready to compile the shaders which can be found in ${CMAKE_BINARY_DIR}/shaders")
    set(SHADER_PATH ${CMAKE_SOURCE_DIR}/engine/shader)
    file(GLOB_RECURSE GLSL_SHADERS
        "${SHADER_PATH}/simple_triangle.vert"
        "${SHADER_PATH}/simple_triangle.frag"
    )
    set(COMPILED_SHADERS "")

    foreach(GLSL ${GLSL_SHADERS})
        get_filename_component(FILE_NAME ${GLSL} NAME)
        set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${FILE_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND ${GLSLC} -DUSE_VULKAN_BACKEND -o ${SPIRV} ${GLSL}
            DEPENDS ${GLSL}
            COMMENT "Compiling shader ${FILE_NAME}"
        )

        list(APPEND COMPILED_SHADERS ${SPIRV})
    endforeach()
   add_custom_target(Shaders ALL DEPENDS ${COMPILED_SHADERS})
endif()

