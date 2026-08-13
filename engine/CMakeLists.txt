add_subdirectory(editor)
add_subdirectory(runtime)
add_subdirectory(module)
add_subdirectory(example)
add_library(EngineLib INTERFACE)

target_link_libraries(EngineLib INTERFACE RuntimeLib EditorLib Module ExampleLib
)

target_include_directories(EngineLib
    INTERFACE ${CMAKE_SOURCE_DIR}/engine
)
target_include_directories(RuntimeLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}")

