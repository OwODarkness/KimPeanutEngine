# The panel representation and import stages. The runtime library deliberately
# links nothing: the matrix, glyph, layout, and product layers are pure CPU data,
# so a Render or Graphics dependency here would be an ownership mistake rather
# than a convenience.
add_library(Panel STATIC
    runtime/ascii_preview.cpp
    runtime/dot_matrix.cpp
    runtime/panel.cpp
    runtime/panel_glyph_product.cpp
    runtime/utf8.cpp)

target_include_directories(Panel PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")

# Bakes a TrueType face into a glyph product. It is a tool-side library, so the
# rasteriser stays out of the runtime path: only the asset tool links this.
add_library(PanelImport STATIC
    import/panel_glyph_import.cpp)

target_include_directories(PanelImport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/import"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime")

# ImGui vendors upstream stb_truetype; including it read-only is cheaper than a
# second copy, which cannot be fetched in this environment anyway.
target_include_directories(PanelImport SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/imgui")

target_link_libraries(PanelImport PUBLIC Panel)

# The panel's render layer, and the only part of the panel that knows a GPU
# exists. Everything under runtime/ stays pure CPU data with no Render link, so
# this boundary is what keeps the representation testable without a device.
add_library(PanelRender STATIC
    render/panel_render_planner.cpp
    render/panel_renderer.cpp)

target_include_directories(PanelRender PUBLIC
    "${CMAKE_SOURCE_DIR}/engine/runtime"
    "${CMAKE_SOURCE_DIR}/engine/runtime/core"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/render")

target_link_libraries(PanelRender PUBLIC Render Panel)

# The standalone panel viewer and the commands it contributes. It is not an
# EngineModule: it has no per-frame module lifecycle, and its whole runtime
# existence is the host the bootstrap registers.
add_library(PanelModule STATIC
    panel_command_provider.cpp
    panel_viewer_host.cpp)

target_include_directories(PanelModule PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")

# No ImGui and no EditorUILib: the panel viewer presents its own output target
# and needs no UI, so it stays clear of the RuntimeLib/EditorLib cycle.
target_link_libraries(PanelModule PUBLIC PanelRender
    PRIVATE RuntimeLib RuntimeScreenshot)
