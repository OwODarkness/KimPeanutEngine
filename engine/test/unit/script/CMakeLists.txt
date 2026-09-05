add_executable(
    ScriptUnitTest
    script_vm_test.cpp
    lua_command_bridge_test.cpp
)

target_link_libraries(
    ScriptUnitTest
    PRIVATE
    Script
    RuntimeCommand
    RuntimeScreenshotCommand
    KP::GoogleTest
)

# Static-only deps (lua53.lib, gtest) — no runtime DLLs to copy.

include(GoogleTest)
gtest_discover_tests(ScriptUnitTest)
