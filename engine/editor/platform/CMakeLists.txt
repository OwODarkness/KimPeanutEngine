add_library(EditorUIPlatform STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_imgui_rhi_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_imgui_glfw_wsi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_imgui_opengl_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_imgui_vulkan_renderer.cpp
)

target_include_directories(EditorUIPlatform PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)
target_link_libraries(EditorUIPlatform
    PUBLIC Graphics KP::ImGui KP::Vulkan
    PRIVATE Window Log KP::Glfw KP::Glad
)
