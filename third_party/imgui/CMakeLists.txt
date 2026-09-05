set(KP_IMGUI_SOURCES
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
    backends/imgui_impl_vulkan.cpp
)

add_library(KPThirdPartyImGui STATIC ${KP_IMGUI_SOURCES})

target_include_directories(KPThirdPartyImGui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

target_link_libraries(KPThirdPartyImGui PRIVATE
    KP::Glfw
    KP::Vulkan
)

add_library(KP::ImGui ALIAS KPThirdPartyImGui)
