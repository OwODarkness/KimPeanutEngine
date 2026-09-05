add_executable(GameplayEditorBridgeUnitTest
    gameplay_editor_bridge_test.cpp
)

target_include_directories(GameplayEditorBridgeUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(GameplayEditorBridgeUnitTest PRIVATE
    GameplayEditorBridge
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(GameplayEditorBridgeUnitTest)
