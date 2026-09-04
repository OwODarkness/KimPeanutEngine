add_library(Render
render_system.cpp
render_scene_coordinator.cpp
prepared_render_asset_catalog.cpp
deferred_renderer.cpp
render_capture_service.cpp
    render_source_registry.cpp
    camera_source_registry.cpp
    environment_source_registry.cpp
light/light_source_registry.cpp
light/light_world.cpp
light/light_gpu_data.cpp
render_resource_resolver.cpp
material/material_system.cpp
material/material_asset_resolver.cpp
render_world/render_world.cpp
render_world/frustum.cpp
render_world/scene_visibility.cpp
render_world/scene_draw_list.cpp
render_world/scene_draw_list_builder.cpp
frame_context.cpp
render_pass.cpp
render_target.cpp
renderer_frame_targets.cpp
render_camera.cpp
camera_utils.cpp
render_scene.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Graphics PRIVATE: RenderSystem owns the API-neutral RenderBackend facade;
# scenes record through the common command-recorder contract.
target_link_libraries(Render PUBLIC Core Asset PRIVATE Graphics )
