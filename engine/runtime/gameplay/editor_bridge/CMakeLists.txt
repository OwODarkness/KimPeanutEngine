add_library(GameplayEditorBridge STATIC
    gameplay_editor_bridge.cpp
)

target_include_directories(GameplayEditorBridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(GameplayEditorBridge
    PUBLIC Gameplay GameplayReflection Reflection
)
