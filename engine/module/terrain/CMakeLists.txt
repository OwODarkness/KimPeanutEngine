add_library(TerrainCore STATIC
    product/terrain_core.cpp)
target_include_directories(TerrainCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(TerrainGeneration STATIC
    evaluation/terrain_generation.cpp)
target_include_directories(TerrainGeneration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TerrainGeneration
    PUBLIC TerrainCore KP::NlohmannJson)

add_library(Terrain INTERFACE)
target_link_libraries(Terrain INTERFACE TerrainGeneration TerrainCore)
