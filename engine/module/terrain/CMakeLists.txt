add_library(TerrainCore STATIC
    product/terrain_core.cpp)
target_include_directories(TerrainCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TerrainCore PUBLIC Data)

add_library(TerrainGeneration STATIC
    evaluation/terrain_generation.cpp
    evaluation/terrain_hydraulic_erosion.cpp)
target_include_directories(TerrainGeneration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TerrainGeneration
    PUBLIC TerrainCore KP::NlohmannJson Math)

add_library(Terrain INTERFACE)
target_link_libraries(Terrain INTERFACE TerrainGeneration TerrainCore)

add_library(TerrainEditor STATIC
    editor/heightmap_debug_view.cpp
    editor/terrain_editor.cpp)
target_include_directories(TerrainEditor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(TerrainEditor PRIVATE
    TerrainGeneration TerrainCore EditorUILib RuntimeLib KP::ImGui)

add_library(TerrainBaking STATIC
    import/terrain_baker.cpp
    import/terrain_material_settings.cpp)
target_include_directories(TerrainBaking PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(TerrainBaking PUBLIC TerrainGeneration RuntimeLib AssetImport AssetNative AssetArchive)

add_library(TerrainViewer STATIC
    tools/terrain_viewer_host.cpp)
target_include_directories(TerrainViewer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_compile_definitions(TerrainViewer PRIVATE
    KPENGINE_TERRAIN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(TerrainViewer PUBLIC TerrainGeneration RuntimeLib TerrainEditor TerrainBaking)
target_link_libraries(Terrain INTERFACE TerrainViewer)

add_library(TerrainRegistration OBJECT
    terrain_registration.cpp)
target_include_directories(TerrainRegistration PRIVATE
    "${CMAKE_SOURCE_DIR}/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TerrainRegistration PRIVATE TerrainModule)

add_library(TerrainModule STATIC
    terrain_module.cpp)
target_include_directories(TerrainModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(TerrainModule PUBLIC TerrainViewer RuntimeLib)
target_link_libraries(Terrain INTERFACE TerrainModule)
