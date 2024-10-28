add_subdirectory(editor)
add_subdirectory(runtime)
add_subdirectory(shader)
add_library(EngineLib INTERFACE)

target_link_libraries(EngineLib INTERFACE RuntimeLib EditorLib ShaderLib)

target_include_directories(EditorLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(RuntimeLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(ShaderLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")