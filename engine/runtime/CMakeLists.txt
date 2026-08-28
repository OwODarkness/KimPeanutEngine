
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(window)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(gameplay)
add_subdirectory(asset)
add_subdirectory(audio)
add_subdirectory(bootstrap)
add_subdirectory(script)

add_library(RuntimeLib STATIC
    engine.cpp
    runtime_global_context.cpp
    )

target_include_directories(RuntimeLib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RuntimeLib PUBLIC Core Graphics Script PRIVATE ThirdPartyLib EditorLib Window Input Render Gameplay Bootstrap Platform)
