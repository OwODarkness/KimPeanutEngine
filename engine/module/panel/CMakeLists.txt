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
