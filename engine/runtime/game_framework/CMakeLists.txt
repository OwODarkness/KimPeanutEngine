add_library(GameFramework STATIC
actor.cpp level.cpp mesh_actor.cpp world.cpp world_system.cpp
)

target_include_directories(GameFramework PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(GameFramework PUBLIC Core Object PRIVATE Render)