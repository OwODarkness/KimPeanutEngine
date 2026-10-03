add_library(Module INTERFACE)
add_subdirectory(tts)
target_link_libraries(Module INTERFACE TTS)

add_library(ModuleBootstrap STATIC
    module_bootstrap.cpp
    module_registration.cpp)
target_include_directories(ModuleBootstrap PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(ModuleBootstrap PRIVATE RuntimeLib)
target_link_libraries(Module INTERFACE ModuleBootstrap)

add_library(AudioPlayerRegistration OBJECT
    "${CMAKE_SOURCE_DIR}/engine/runtime/audio/editor/audio_player_registration.cpp")
target_include_directories(AudioPlayerRegistration PRIVATE
    "${CMAKE_SOURCE_DIR}/engine/runtime/audio/editor"
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerRegistration PRIVATE AudioPlayerMode ModuleBootstrap)
target_link_libraries(Module INTERFACE AudioPlayerMode)
target_sources(Module INTERFACE $<TARGET_OBJECTS:AudioPlayerRegistration>)

target_link_libraries(Module INTERFACE TtsEditorMode)
add_library(TtsEditorRegistration OBJECT
    "${CMAKE_SOURCE_DIR}/engine/module/tts/editor/tts_editor_registration.cpp")
target_include_directories(TtsEditorRegistration PRIVATE
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(TtsEditorRegistration PRIVATE TtsEditorMode ModuleBootstrap)
target_sources(Module INTERFACE $<TARGET_OBJECTS:TtsEditorRegistration>)

if(KPENGINE_ENABLE_TERRAIN)
    add_subdirectory(terrain)
    target_link_libraries(Module INTERFACE Terrain)
    target_sources(Module INTERFACE $<TARGET_OBJECTS:TerrainRegistration>)
endif()

# Panel owns its host registration callback and runtime host. Its representation
# library remains separately usable by tests and tools.
add_subdirectory(panel)
target_link_libraries(Module INTERFACE PanelModule)
target_sources(Module INTERFACE $<TARGET_OBJECTS:PanelRegistration>)

if(KPENGINE_ENABLE_LIVE2D)
    add_subdirectory(live2d)
    target_link_libraries(Module INTERFACE Live2D Live2DModule)
    target_sources(Module INTERFACE $<TARGET_OBJECTS:Live2DModuleRegistration>)
endif()
