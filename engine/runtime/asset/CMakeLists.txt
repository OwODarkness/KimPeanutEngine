# AssetProduct is deliberately database-free. It owns content hashes and
# canonical product paths used by both runtime loading and offline import.
add_library(AssetProduct STATIC
    model_archive.cpp
)
target_compile_definitions(AssetProduct PRIVATE KPENGINE_MODEL_ARCHIVE_PRODUCT_ONLY)
target_include_directories(AssetProduct PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# The archive database remains authoring state and is kept in a separate target.
# Runtime links it only for the strict read-only Level logical-name resolver;
# native product loading and dependency resolution remain database-free.
add_library(AssetArchive STATIC
    model_archive.cpp
)
target_compile_definitions(AssetArchive PRIVATE KPENGINE_MODEL_ARCHIVE_DATABASE_ONLY)
target_include_directories(AssetArchive PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(AssetArchive PUBLIC AssetProduct Database PRIVATE Log)

add_library(AssetNative STATIC
    native_model.cpp
    native_texture.cpp
)
target_include_directories(AssetNative PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(AssetNative PUBLIC AssetProduct Data Base)

# Pure foreign-format decoding is reusable by the offline importer and the
# optional legacy runtime compatibility path. It has no AssetManager state.
add_library(AssetForeignDecode STATIC
    assimp_model_decoder.cpp
)
target_include_directories(AssetForeignDecode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(AssetForeignDecode PUBLIC Data Base PRIVATE ImageIO KP::Assimp KP::NlohmannJson)

option(KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT
       "Enable transitional OBJ/FBX/glTF/GLB runtime loading through Assimp"
       ON)

# This option is temporary migration compatibility. Removal requires the gates
# documented in docs/asset/.plan/MI1.7.md and MI1.8's packaging/visual evidence.

add_library(AssetRuntime STATIC
    asset_manager.cpp
    asset_type_registry.cpp
    asset_load_observation.cpp
    native_model_loader.cpp
    native_texture_loader.cpp
    model.cpp
    shader_program.cpp
    shader_program_loader.cpp
    material_loader.cpp
    level_loader.cpp
    miniaudio_audio_loader.cpp
)
if(KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT)
    target_sources(AssetRuntime PRIVATE assimp_model_loader.cpp)
    target_compile_definitions(AssetRuntime PRIVATE KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT=1)
endif()
target_include_directories(AssetRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(AssetRuntime PUBLIC Data Base AssetNative
    PRIVATE Config ImageIO Log KP::MagicEnum KP::NlohmannJson KP::Miniaudio)
target_link_libraries(AssetRuntime PRIVATE AssetArchive)
if(KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT)
    target_link_libraries(AssetRuntime PRIVATE AssetForeignDecode KP::Assimp)
endif()

# The provider contract is database-free and can be embedded by a small
# offline tool without AssetRuntime or the archive repository.
add_library(AssetImportRegistry STATIC
    asset_import_registry.cpp
)
target_include_directories(AssetImportRegistry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Offline model/material import is intentionally independent of AssetRuntime.
# It may be used by a command-line importer while the engine application is
# not running.
add_library(AssetImport STATIC
    asset_import_adapters.cpp
    native_material.cpp
    material_promotion.cpp
    model_import_service.cpp
    texture_importer.cpp
)
target_include_directories(AssetImport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(AssetImport PUBLIC AssetImportRegistry AssetArchive AssetNative Data Base
    PRIVATE Config ImageIO Log KP::NlohmannJson KP::Assimp AssetForeignDecode)

# Compatibility target name retained for existing runtime consumers. New code
# should choose AssetRuntime, AssetImport, or AssetArchive explicitly.
add_library(Asset STATIC
    asset_compatibility.cpp
)
target_link_libraries(Asset PUBLIC AssetRuntime)
