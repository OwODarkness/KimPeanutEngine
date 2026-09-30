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

if(KPENGINE_ENABLE_TERRAIN)
    add_subdirectory(terrain)
    target_link_libraries(Module INTERFACE Terrain)
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
