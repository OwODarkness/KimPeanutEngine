# Profile metric tests. Compiles the metric sources directly (no EditorLib) so the test
# doesn't drag in RuntimeLib/imgui/glad; the metrics only need the std lib (samplers are
# injected), keeping Win32/psapi out of the test.
add_executable(
    ProfileTest
    profile_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/profile/editor_metric.cpp
    ${CMAKE_SOURCE_DIR}/engine/editor/profile/editor_builtin_metrics.cpp
)

target_include_directories(ProfileTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(ProfileTest PRIVATE googletest)

include(GoogleTest)
gtest_discover_tests(ProfileTest)
