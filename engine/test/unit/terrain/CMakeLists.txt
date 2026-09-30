add_executable(TerrainGenerationTest terrain_generation_test.cpp)
target_link_libraries(TerrainGenerationTest PRIVATE TerrainGeneration KP::GoogleTest)
target_compile_definitions(TerrainGenerationTest PRIVATE
    KPENGINE_TERRAIN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../module/terrain/fixtures")

include(GoogleTest)
gtest_discover_tests(TerrainGenerationTest PROPERTIES LABELS "terrain")
