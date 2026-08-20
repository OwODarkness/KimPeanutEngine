add_library(Render
render_system.cpp
frame_context.cpp
render_camera.cpp
render_scene.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Graphics PRIVATE: RenderSystem owns the API-neutral RenderBackend facade;
# RenderScene remains Vulkan-specific until the common recorder lands.
target_link_libraries(Render PUBLIC Core Asset PRIVATE Graphics )
