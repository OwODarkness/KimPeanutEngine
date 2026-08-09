add_library(GameFramework STATIC
actor.cpp level.cpp mesh_actor.cpp world.cpp world_system.cpp
camera.cpp
)

target_include_directories(GameFramework PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
${CMAKE_CURRENT_SOURCE_DIR}/../..
PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}

)

target_link_libraries(GameFramework PUBLIC Core Object Component Input PRIVATE Render glfw)