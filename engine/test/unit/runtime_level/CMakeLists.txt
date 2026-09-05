add_executable(RuntimeLevelTest
    runtime_level_test.cpp
)

target_include_directories(RuntimeLevelTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(RuntimeLevelTest PRIVATE RuntimeLevel KP::GoogleTest)

# AssetManager constructs the Assimp loader during process startup. Keep the
# focused test executable self-contained so GoogleTest discovery works on
# Windows without relying on another target's output directory.
add_custom_command(TARGET RuntimeLevelTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:RuntimeLevelTest>
)

include(GoogleTest)
gtest_discover_tests(RuntimeLevelTest)
