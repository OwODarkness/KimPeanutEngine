add_library(RuntimeScreenshot STATIC
    runtime_screenshot_service.cpp
)

target_include_directories(RuntimeScreenshot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimeScreenshot
    PUBLIC Render
    PRIVATE ImageIO
)
