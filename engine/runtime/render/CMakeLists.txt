add_library(Render 
aabb.cpp aabb_debugger.cpp render_system.cpp directional_shadow_caster.cpp environment_map.cpp frame_buffer.cpp frustum.cpp gizmos.cpp mesh_scene_proxy.cpp 
model_loader.cpp outline_effect.cpp point_shadow_caster.cpp pointcloud_scene_proxy.cpp postprocess_pipeline.cpp
primitive_scene_proxy.cpp render_camera.cpp render_material.cpp render_mesh_resource.cpp render_mesh.cpp 
render_pointcloud.cpp render_scene.cpp render_shader.cpp render_texture.cpp shader_pool.cpp shadow_caster.cpp
shadow_manager.cpp skybox.cpp spot_shadow_caster.cpp texture_pool.cpp
)

target_include_directories(Render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Render PUBLIC Core PRIVATE GameFramework Component Input)