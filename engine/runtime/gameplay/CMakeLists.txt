add_library(Gameplay STATIC
    gameplay_module.cpp
    actor/actor.cpp
    component/actor_component.cpp
    component/scene_component.cpp
    component/camera_component.cpp
    controller/player_controller.cpp
    component/primitive_component.cpp
    component/mesh_component.cpp
    component/directional_light_component.cpp
    factory/static_mesh_actor_factory.cpp
    factory/directional_light_actor_factory.cpp
    factory/free_camera_actor_factory.cpp
    world/gameplay_world.cpp
)

target_include_directories(Gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Gameplay consumes only the render-owned source contract. Graphics remains a
# private Render dependency, so gameplay cannot reach backend APIs.
target_link_libraries(Gameplay PUBLIC Core Render Input)
