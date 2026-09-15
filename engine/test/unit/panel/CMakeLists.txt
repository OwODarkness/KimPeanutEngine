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
gtest_discover_tests(PanelUnitTest)
