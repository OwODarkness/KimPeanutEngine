add_executable(RuntimeScreenshotServiceTest
    runtime_screenshot_service_test.cpp
)

target_include_directories(RuntimeScreenshotServiceTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(RuntimeScreenshotServiceTest PRIVATE RuntimeScreenshot googletest)

include(GoogleTest)
gtest_discover_tests(RuntimeScreenshotServiceTest)

add_executable(ScreenshotCommandProviderTest
    screenshot_command_provider_test.cpp
)

target_include_directories(ScreenshotCommandProviderTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(ScreenshotCommandProviderTest PRIVATE RuntimeScreenshotCommand googletest)

include(GoogleTest)
gtest_discover_tests(ScreenshotCommandProviderTest)

add_executable(RuntimeStartupTest
    runtime_startup_test.cpp
)

target_include_directories(RuntimeStartupTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(RuntimeStartupTest PRIVATE RuntimeLib googletest)

# RuntimeLib contains AssetManager's Assimp loader even though these tests use
# synthetic LevelResources. Keep test discovery runnable on Windows.
add_custom_command(TARGET RuntimeStartupTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:RuntimeStartupTest>
)

include(GoogleTest)
gtest_discover_tests(RuntimeStartupTest)

add_executable(RuntimeLaunchOptionsTest
    launch_options_test.cpp
)

target_include_directories(RuntimeLaunchOptionsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(RuntimeLaunchOptionsTest PRIVATE RuntimeLib googletest)

add_custom_command(TARGET RuntimeLaunchOptionsTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/assimp/bin/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:RuntimeLaunchOptionsTest>
)

include(GoogleTest)
gtest_discover_tests(RuntimeLaunchOptionsTest)
