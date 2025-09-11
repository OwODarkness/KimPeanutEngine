cmake_minimum_required(VERSION 3.5.0)

option(USE_OPENGL "Use OpenGL backend" ON)
option(USE_VULKAN "Use Vulkan backend" ON)

add_library(Graphics STATIC
    backend/common/render_backend.cpp
    backend/common/shader_manager.cpp
    
    backend/opengl/opengl_backend.cpp
    backend/opengl/opengl_texture.cpp
    backend/opengl/opengl_shader.cpp

    backend/vulkan/vulkan_backend.cpp
    backend/vulkan/vulkan_texture.cpp
    backend/vulkan/vulkan_shader.cpp
    backend/vulkan/vulkan_buffer_pool.cpp
    backend/vulkan/vulkan_memory_linear_allocator.cpp
    backend/vulkan/vulkan_memory_dedicated_allocator.cpp
    backend/vulkan/vulkan_memory_pool_allocator.cpp
    backend/vulkan/vulkan_pipeline_manager.cpp
    tool/shaderloader.cpp
)

target_include_directories(Graphics 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tool/..
)

target_link_libraries(Graphics PUBLIC Core )

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
    message(STATUS "Found glslc: ${GLSLC}")
    set(SHADER_PATH ${CMAKE_SOURCE_DIR}/engine/shader)
    file(GLOB_RECURSE GLSL_SHADERS
        "${SHADER_PATH}/*.vert"
        "${SHADER_PATH}/*.frag"
        "${SHADER_PATH}/*.geom"
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
   # add_custom_target(Shaders ALL DEPENDS ${COMPILED_SHADERS})
endif()

