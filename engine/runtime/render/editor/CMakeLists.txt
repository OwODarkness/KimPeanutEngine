add_library(RenderEditor STATIC
    debug_viewer_view.cpp
    gpu_profiler_view.cpp
)

target_include_directories(RenderEditor PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(RenderEditor PUBLIC Render UIWidgets KP::NlohmannJson)
