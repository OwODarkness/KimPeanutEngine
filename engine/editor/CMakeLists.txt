# EditorLib is the backend-agnostic editor-tool layer. EditorUILib owns ImGui,
# WSI, and graphics-API-specific UI renderers; keeping it separate prevents
# Vulkan/OpenGL implementation dependencies from leaking into editor tools.
# Sources are listed explicitly through target_sources (no GLOB). main.cpp is
# the exe entry (root CMakeLists.txt) and deliberately stays out of both libs.
add_library(EditorLib STATIC
    editor.cpp
)

add_library(EditorUILib STATIC)

add_subdirectory(context)
add_subdirectory(ui)
add_subdirectory(platform)
add_subdirectory(log)
add_subdirectory(settings)
add_subdirectory(profile)

target_include_directories(EditorLib PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)

target_include_directories(EditorUILib PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(EditorLib PRIVATE RuntimeLib EditorUILib)
target_link_libraries(EditorUILib PRIVATE RuntimeLib RuntimeCommand Input imgui glad VulkanSDK nlohmann)
