add_library(Render
render_system.cpp
render_scene_coordinator.cpp
prepared_render_asset_catalog.cpp
deferred_renderer.cpp
passes/deferred_lighting_pass.cpp
passes/screen_space_ao_pass.cpp
passes/environment_bindings_owner.cpp
passes/tone_map_pass.cpp
passes/capture_view_pass.cpp
passes/path_tracing_pass.cpp
passes/fullscreen_pass_resources.cpp
passes/scene_draw_recorder.cpp
passes/shadow_pass.cpp
passes/shadow_pass_utils.cpp
ray_tracing/ray_tracing_scene.cpp
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
render_submission.cpp
render_submission_executor.cpp
render_submission_frame_context.cpp
render_pass_declaration.cpp
render_target.cpp
renderer_frame_targets.cpp
render_camera.cpp
temporal_reprojection.cpp
submitted_transform_history.cpp
camera_utils.cpp
render_scene.cpp
render_profile.cpp
render_graph/render_graph.cpp
render_graph/render_graph_frame.cpp
render_graph/render_graph_bindings.cpp
render_graph/render_graph_executor.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Graphics PRIVATE: RenderSystem owns the API-neutral RenderBackend facade;
# scenes record through the common command-recorder contract.
target_link_libraries(Render PUBLIC Core Asset PRIVATE Graphics )
