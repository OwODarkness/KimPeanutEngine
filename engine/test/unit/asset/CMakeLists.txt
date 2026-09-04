add_executable(
    AssetUnitTest
    assimp_model_loader_test.cpp
    asset_load_observation_test.cpp
    material_loader_test.cpp
    level_loader_test.cpp
    shader_program_variant_test.cpp
)

target_link_libraries(AssetUnitTest PRIVATE Asset Config googletest)

# AssetManager constructs the current Assimp model loader even though these
# material-only tests never load a model. Copy its runtime dependency so CMake's
# test discovery can execute this target on Windows.
add_custom_command(TARGET AssetUnitTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:AssetUnitTest>
)

include(GoogleTest)
gtest_discover_tests(AssetUnitTest)
