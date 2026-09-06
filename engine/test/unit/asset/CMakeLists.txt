add_executable(
    AssetUnitTest
    assimp_model_loader_test.cpp
    asset_load_observation_test.cpp
    material_loader_test.cpp
    level_loader_test.cpp
    shader_program_variant_test.cpp
)

target_link_libraries(AssetUnitTest PRIVATE AssetRuntime Config KP::GoogleTest)

target_compile_definitions(AssetUnitTest PRIVATE
    KPENGINE_MI1_1_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mi1_1"
)

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

add_executable(
    ModelArchiveDatabaseTest
    model_archive_database_test.cpp
)

target_link_libraries(ModelArchiveDatabaseTest PRIVATE AssetArchive Config KP::GoogleTest)

add_custom_command(TARGET ModelArchiveDatabaseTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:ModelArchiveDatabaseTest>
)

include(GoogleTest)
gtest_discover_tests(ModelArchiveDatabaseTest)

add_executable(
    ModelDecoderTest
    model_decoder_test.cpp
)

target_link_libraries(ModelDecoderTest PRIVATE AssetForeignDecode Config KP::GoogleTest)

target_compile_definitions(ModelDecoderTest PRIVATE
    KPENGINE_MI1_1_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mi1_1"
)

add_custom_command(TARGET ModelDecoderTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:ModelDecoderTest>
)

include(GoogleTest)
gtest_discover_tests(ModelDecoderTest)

add_executable(
    NativeModelTest
    native_model_test.cpp
)

target_link_libraries(NativeModelTest PRIVATE AssetRuntime AssetArchive Config KP::GoogleTest)

target_compile_definitions(NativeModelTest PRIVATE
    KPENGINE_NATIVE_RUNTIME_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/native_runtime"
)

add_custom_command(TARGET NativeModelTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:NativeModelTest>
)

include(GoogleTest)
gtest_discover_tests(NativeModelTest)

add_executable(
    NativeMaterialTest
    native_material_test.cpp
)

target_link_libraries(NativeMaterialTest PRIVATE AssetImport AssetRuntime Config KP::GoogleTest)

add_custom_command(TARGET NativeMaterialTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:NativeMaterialTest>
)

include(GoogleTest)
gtest_discover_tests(NativeMaterialTest)

add_executable(
    ModelImportServiceTest
    model_import_service_test.cpp
)

target_link_libraries(ModelImportServiceTest PRIVATE AssetImport Config KP::GoogleTest)

add_custom_command(TARGET ModelImportServiceTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:ModelImportServiceTest>
)

include(GoogleTest)
gtest_discover_tests(ModelImportServiceTest)
