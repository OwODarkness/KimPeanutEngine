
add_subdirectory(core)
add_subdirectory(reflection)
add_subdirectory(platform)
add_subdirectory(window)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(gameplay)
add_subdirectory(image_io)
add_subdirectory(asset)
add_subdirectory(audio)
add_subdirectory(bootstrap)
add_subdirectory(command)
add_subdirectory(script)
add_subdirectory(screenshot)
add_subdirectory(level)

add_library(RuntimeLib STATIC
    engine.cpp
    launch_options.cpp
    runtime_global_context.cpp
    runtime_startup.cpp
    render_asset_preparer.cpp
    )

target_include_directories(RuntimeLib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RuntimeLib PUBLIC Core Graphics Script PRIVATE EditorLib Window Input Render Gameplay GameplayReflection RuntimeLevel Bootstrap Platform ImageIO RuntimeScreenshot RuntimeCommand RuntimeScreenshotCommand)
