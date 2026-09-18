add_executable(
    PanelUnitTest
    dot_matrix_test.cpp
    glyph_cell_test.cpp
    panel_glyph_import_test.cpp
    panel_glyph_product_test.cpp
    panel_test.cpp
    utf8_test.cpp
)

target_link_libraries(PanelUnitTest PRIVATE Panel PanelImport KP::GoogleTest)

# The import tests need a TrueType face with CJK coverage. Without one they are
# skipped rather than failed, the same way Live2DCoreTest treats its optional
# .moc3 fixture. Point this at any CJK face to run them.
set(KPENGINE_PANEL_FONT_PATH "C:/Windows/Fonts/simhei.ttf"
    CACHE FILEPATH "TrueType face used by the panel glyph import tests")
if(KPENGINE_PANEL_FONT_PATH AND EXISTS "${KPENGINE_PANEL_FONT_PATH}")
    target_compile_definitions(PanelUnitTest PRIVATE
        KPENGINE_PANEL_FONT_PATH="${KPENGINE_PANEL_FONT_PATH}")
endif()

include(GoogleTest)
gtest_discover_tests(PanelUnitTest PROPERTIES LABELS "panel")

# CPU-only render contract test. It compiles the planner and the generic
# submission together and links neither Render nor a backend, so it runs
# everywhere and cannot be affected by device state.
add_executable(PanelRenderPlannerTest
    panel_render_planner_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/panel/render/panel_render_planner.cpp
    ${CMAKE_SOURCE_DIR}/engine/runtime/render/render_submission.cpp)

target_include_directories(PanelRenderPlannerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/panel/render
    ${CMAKE_SOURCE_DIR}/engine/module/panel/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend)

target_link_libraries(PanelRenderPlannerTest PRIVATE KP::GoogleTest)

gtest_discover_tests(PanelRenderPlannerTest PROPERTIES LABELS "panel")

# The renderer against the shared backend test double: no device, no window, and
# no Cubism, so the texture-swap and handle-lifetime rules are assertable in CI.
add_executable(PanelRendererTest
    panel_renderer_test.cpp)

target_include_directories(PanelRendererTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/panel/runtime
    ${CMAKE_SOURCE_DIR}/engine/test
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
    ${CMAKE_SOURCE_DIR}/engine/runtime/graphics/backend)

target_link_libraries(PanelRendererTest PRIVATE PanelRender Panel KP::GoogleTest)

# Linking Render pulls the asset importer, which ships as a runtime DLL. Without
# this the executable cannot start at all, and gtest discovery reports a
# discovery failure rather than a build error.
copy_runtime_dependencies(PanelRendererTest)

gtest_discover_tests(PanelRendererTest PROPERTIES LABELS "panel")

# The command contract: what the provider accepts, rejects, and reports. It
# compiles the provider directly and links only the command lane, so it needs no
# host, no window, and no device.
add_executable(PanelCommandProviderTest
    panel_command_provider_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/module/panel/panel_command_provider.cpp)

target_include_directories(PanelCommandProviderTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/module/panel
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core)

target_link_libraries(PanelCommandProviderTest PRIVATE RuntimeCommand KP::GoogleTest)

gtest_discover_tests(PanelCommandProviderTest PROPERTIES LABELS "panel")
