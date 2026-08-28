add_library(GraphicsExample STATIC
rhi_example.cpp
graphics_example.cpp)

target_include_directories(GraphicsExample PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(GraphicsExample 
PRIVATE
Graphics
Render
Gameplay
Window
Input
)

add_executable(GraphicsSmoke graphics_smoke_main.cpp)
target_link_libraries(GraphicsSmoke PRIVATE GraphicsExample)
copy_runtime_dependencies(GraphicsSmoke)


