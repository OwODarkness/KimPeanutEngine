add_subdirectory(editor)
add_subdirectory(runtime)
add_subdirectory(module)
add_library(EngineLib INTERFACE)

target_link_libraries(EngineLib INTERFACE RuntimeLib EditorLib Module
)

target_include_directories(EngineLib
    INTERFACE ${CMAKE_SOURCE_DIR}/engine   # gives you "runtime/..."
)
target_include_directories(EditorLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(RuntimeLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")

