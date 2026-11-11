add_library(UIWidgets STATIC
    ui_backend.cpp
    ui_plot.cpp
    ui_control_validation.cpp
)
target_include_directories(UIWidgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(UIRecordingBackend STATIC
    ui_recording_backend.cpp
)
target_include_directories(UIRecordingBackend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(UIRecordingBackend PUBLIC UIWidgets)

add_library(UIPresentation STATIC
    ui_session.cpp
)
target_include_directories(UIPresentation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(UIPresentation PUBLIC UIWidgets)

add_library(UIBackendImGui STATIC
    ui_imgui_backend.cpp
)
target_include_directories(UIBackendImGui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(UIBackendImGui PUBLIC UIWidgets PRIVATE KP::ImGui)
