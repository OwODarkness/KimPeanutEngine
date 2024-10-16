cmake_minimum_required(VERSION 3.5.0)
project(KimPeanutEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    engine/editor/include
    engine/editor/include/editor_ui_component
    engine
    third_party
    )


    
add_executable(KimPeanutEngine 
    engine/editor/main.cpp
    engine/editor/src/editor_ui.cpp
    engine/editor/src/editor.cpp
    engine/editor/src/editor_ui_component.cpp
    engine/editor/src/editor_ui_component/editor_text_component.cpp
    engine/editor/src/editor_ui_component/editor_tooltip_component.cpp
    engine/editor/src/editor_ui_component/editor_button_component.cpp
    engine/runtime/engine.cpp
    engine/runtime/core/log/logger.cpp


    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_impl_opengl3.cpp
    third_party/imgui/imgui_impl_glfw.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_demo.cpp

)

target_link_libraries(KimPeanutEngine 
${CMAKE_SOURCE_DIR}/third_party/glad/lib/glad.lib
${CMAKE_SOURCE_DIR}/third_party/GLFW/lib/glfw3.lib
)
