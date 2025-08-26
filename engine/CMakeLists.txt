add_subdirectory(editor)
add_subdirectory(runtime)

add_library(EngineLib INTERFACE)

target_link_libraries(EngineLib INTERFACE RuntimeLib EditorLib )

target_include_directories(EditorLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(RuntimeLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")

