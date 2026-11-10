add_library(RenderSettings STATIC
    render_settings.cpp
)

target_include_directories(RenderSettings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(RenderSettings PUBLIC Render RuntimeSettings)
