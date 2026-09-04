add_executable(EditorUILifecycleTest
    editor_ui_lifecycle_test.cpp
)

target_include_directories(EditorUILifecycleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(EditorUILifecycleTest PRIVATE
    EditorUILib
    imgui
    googletest
)

add_custom_command(TARGET EditorUILifecycleTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:EditorUILifecycleTest>
)

include(GoogleTest)
gtest_discover_tests(EditorUILifecycleTest)
