add_executable(TerrainGenerationTest terrain_generation_test.cpp)
target_link_libraries(TerrainGenerationTest PRIVATE TerrainGeneration KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(TerrainGenerationTest PROPERTIES LABELS "terrain")
