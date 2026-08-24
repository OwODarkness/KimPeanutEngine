add_executable(
    RenderPassScheduleTest
    render_pass_schedule_test.cpp
    mesh_proxy_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_pass.cpp
)

target_include_directories(RenderPassScheduleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend
)

target_link_libraries(RenderPassScheduleTest PRIVATE googletest Math)

include(GoogleTest)
gtest_discover_tests(RenderPassScheduleTest)
