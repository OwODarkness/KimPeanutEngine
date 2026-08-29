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
