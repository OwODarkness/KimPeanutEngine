add_library(Spatial STATIC
    linear_bvh.cpp
)

target_include_directories(Spatial PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Spatial PUBLIC Math)
