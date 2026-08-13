add_executable(
    ScriptUnitTest
    script_vm_test.cpp
)

target_link_libraries(
    ScriptUnitTest
    PRIVATE
    Script
    googletest
)

# Static-only deps (lua53.lib, gtest) — no runtime DLLs to copy.

include(GoogleTest)
gtest_discover_tests(ScriptUnitTest)
