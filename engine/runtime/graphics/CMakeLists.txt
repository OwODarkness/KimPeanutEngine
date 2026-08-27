cmake_minimum_required(VERSION 3.5.0)

option(USE_OPENGL "Build the OpenGL backend" ON)
option(USE_VULKAN "Build the Vulkan backend" ON)

set(GRAPHICS_COMMON_SOURCES
    backend/common/bindless_texture.cpp
    backend/common/render_backend.cpp
    backend/common/pipeline_validation.cpp
    backend/common/texture_manager.cpp
    backend/common/sampler_manager.cpp
    backend/common/mesh_manager.cpp
)

set(GRAPHICS_BACKEND_SOURCES)
if(USE_OPENGL)
    list(APPEND GRAPHICS_BACKEND_SOURCES
    backend/opengl/opengl_backend.cpp
    backend/opengl/opengl_bindless_texture_table.cpp
    backend/opengl/opengl_texture.cpp
    backend/opengl/opengl_sampler.cpp
    backend/opengl/opengl_mesh.cpp
    backend/opengl/opengl_pipeline.cpp
    backend/opengl/opengl_pipeline_manager.cpp
    backend/opengl/opengl_descriptorset.cpp
    )
endif()

if(USE_VULKAN)
    list(APPEND GRAPHICS_BACKEND_SOURCES
    backend/vulkan/vulkan_backend.cpp
    backend/vulkan/vulkan_bindless_texture_table.cpp
    backend/vulkan/vulkan_device.cpp
    backend/vulkan/vulkan_swapchain.cpp
    backend/vulkan/vulkan_frame_context.cpp
    backend/vulkan/vulkan_command_recorder.cpp
    backend/vulkan/vulkan_render_target_manager.cpp
    backend/vulkan/vulkan_editor_bridge.cpp
    backend/vulkan/vulkan_upload_context.cpp
    backend/vulkan/vulkan_texture.cpp
    backend/vulkan/vulkan_buffer_manager.cpp
    backend/vulkan/vulkan_memory_manager.cpp
    backend/vulkan/vulkan_memory_dedicated_allocator.cpp
    backend/vulkan/vulkan_memory_pool_allocator.cpp
    backend/vulkan/vulkan_pipeline_manager.cpp
    backend/vulkan/vulkan_descriptor_set_manager.cpp
    backend/vulkan/vulkan_image_memory_manager.cpp
    backend/vulkan/vulkan_sampler.cpp
    backend/vulkan/vulkan_mesh.cpp
    )
endif()

if(NOT USE_OPENGL AND NOT USE_VULKAN)
    message(FATAL_ERROR "Graphics requires USE_OPENGL or USE_VULKAN")
endif()

add_library(Graphics STATIC
    ${GRAPHICS_COMMON_SOURCES}
    ${GRAPHICS_BACKEND_SOURCES}
)

target_include_directories(Graphics
    # Backend source files can include common/, vulkan/, and opengl/ directly.
    # Consumers receive no backend include root from this target.
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tool/..
)

target_link_libraries(Graphics
    PUBLIC Core
    PRIVATE Data Asset glfw glad VulkanSDK
)

target_compile_definitions(Graphics PRIVATE
    KPENGINE_GRAPHICS_ENABLE_OPENGL=$<BOOL:${USE_OPENGL}>
    KPENGINE_GRAPHICS_ENABLE_VULKAN=$<BOOL:${USE_VULKAN}>
)

