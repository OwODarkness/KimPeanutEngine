add_library(VulkanBackend STATIC
  vulkan_backend.cpp
    vulkan_texture.cpp
    vulkan_shader.cpp
    vulkan_buffer_manager.cpp
    vulkan_memory_linear_allocator.cpp
    vulkan_memory_dedicated_allocator.cpp
    vulkan_memory_pool_allocator.cpp
    vulkan_pipeline_manager.cpp
    vulkan_image_memory_manager.cpp
    vulkan_sampler.cpp
    vulkan_mesh.cpp
)

target_include_directories(VulkanBackend
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(VulkanBackend PUBLIC USE_VULKAN)
target_link_libraries(VulkanBackend PUBLIC VulkanSDK Core PRIVATE Data Asset glfw )
