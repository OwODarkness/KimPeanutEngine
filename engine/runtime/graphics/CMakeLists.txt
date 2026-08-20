cmake_minimum_required(VERSION 3.5.0)

option(USE_OPENGL "Use OpenGL backend" ON)
option(USE_VULKAN "Use Vulkan backend" ON)

add_library(Graphics STATIC
    backend/common/render_backend.cpp
    backend/common/texture_manager.cpp
    backend/common/sampler_manager.cpp
    backend/common/mesh_manager.cpp

    backend/opengl/opengl_backend.cpp
    backend/opengl/opengl_texture.cpp
    backend/opengl/opengl_sampler.cpp
    backend/opengl/opengl_mesh.cpp
    backend/opengl/opengl_pipeline.cpp
    backend/opengl/opengl_pipeline_manager.cpp
    backend/opengl/opengl_descriptorset.cpp

    backend/vulkan/vulkan_backend.cpp
    backend/vulkan/vulkan_device.cpp
    backend/vulkan/vulkan_swapchain.cpp
    backend/vulkan/vulkan_frame_context.cpp
    backend/vulkan/vulkan_texture.cpp
    backend/vulkan/vulkan_buffer_manager.cpp
    backend/vulkan/vulkan_memory_linear_allocator.cpp
    backend/vulkan/vulkan_memory_dedicated_allocator.cpp
    backend/vulkan/vulkan_memory_pool_allocator.cpp
    backend/vulkan/vulkan_pipeline_manager.cpp
    backend/vulkan/vulkan_descriptor_set_manager.cpp
    backend/vulkan/vulkan_image_memory_manager.cpp
    backend/vulkan/vulkan_sampler.cpp
    backend/vulkan/vulkan_mesh.cpp
    )

target_include_directories(Graphics 
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tool/..
)

target_link_libraries(Graphics
    PUBLIC Core glad VulkanSDK
    PRIVATE Data Asset glfw )

