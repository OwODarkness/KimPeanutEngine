add_library(Module INTERFACE)
add_subdirectory(tts)
target_link_libraries(Module INTERFACE TTS)

add_library(ModuleBootstrap STATIC
    module_bootstrap.cpp)
target_include_directories(ModuleBootstrap PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(ModuleBootstrap PRIVATE RuntimeLib EditorUILib)
target_link_libraries(Module INTERFACE ModuleBootstrap)

if(KPENGINE_ENABLE_LIVE2D)
    add_subdirectory(live2d)
    target_compile_definitions(ModuleBootstrap PRIVATE
        KPENGINE_ENABLE_LIVE2D=1)
    target_link_libraries(ModuleBootstrap PRIVATE Live2DModule Live2DEditor)
    target_link_libraries(Module INTERFACE Live2D)
else()
    target_compile_definitions(ModuleBootstrap PRIVATE
        KPENGINE_ENABLE_LIVE2D=0)
endif()
