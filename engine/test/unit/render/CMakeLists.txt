add_executable(
    RenderPassScheduleTest
    render_pass_schedule_test.cpp
    mesh_proxy_test.cpp
    render_world_test.cpp
    frustum_test.cpp
    scene_visibility_test.cpp
    scene_draw_list_test.cpp
    material_system_test.cpp
    material_asset_resolver_test.cpp
    render_source_registry_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_pass.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/material/material_system.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/material/material_asset_resolver.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_camera.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/render_world.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_source_registry.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/frustum.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/scene_visibility.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_world/scene_draw_list.cpp
)

target_include_directories(RenderPassScheduleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend
)

target_link_libraries(RenderPassScheduleTest PRIVATE Asset googletest Math)

add_custom_command(TARGET RenderPassScheduleTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:RenderPassScheduleTest>
)

include(GoogleTest)
gtest_discover_tests(RenderPassScheduleTest)
