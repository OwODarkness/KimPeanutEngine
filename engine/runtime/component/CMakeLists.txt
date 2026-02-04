add_library(Component STATIC 
actor_component.cpp 
mesh_component.cpp
pointcloud_component.cpp
primitive_component.cpp
scene_component.cpp
camera_component.cpp
)

target_include_directories(Component PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Component 
PUBLIC Math Object PRIVATE Render RuntimeLib )