add_executable(
    RenderPassScheduleTest
    render_pass_schedule_test.cpp
    mesh_proxy_test.cpp
    render_world_test.cpp
    frustum_test.cpp
    scene_visibility_test.cpp
    scene_draw_list_test.cpp
    material_system_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_pass.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/material/material_system.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_camera.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/render_world.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/frustum.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/scene_visibility.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/scene_draw_list.cpp
)

target_include_directories(RenderPassScheduleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend
)

target_link_libraries(RenderPassScheduleTest PRIVATE googletest Math)

include(GoogleTest)
gtest_discover_tests(RenderPassScheduleTest)
