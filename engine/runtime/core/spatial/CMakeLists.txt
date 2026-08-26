add_library(Spatial INTERFACE)

target_include_directories(Spatial INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Spatial INTERFACE Math)
