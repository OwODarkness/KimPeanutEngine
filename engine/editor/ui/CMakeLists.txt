target_sources(EditorUILib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_extension_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_panel_command_provider.cpp
)

add_library(EditorUISessionLib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_ui_session_commands.cpp)
target_include_directories(EditorUISessionLib PUBLIC
    ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(EditorUISessionLib
    PUBLIC RuntimeCommand
    PRIVATE KP::NlohmannJson)
add_library(EditorUiHostLib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/host/editor_ui_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_theme.cpp
)
target_include_directories(EditorUiHostLib PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/engine/runtime)
target_link_libraries(EditorUiHostLib
    PUBLIC UIWidgets
    PRIVATE UIPresentation UIBackendImGui EditorUIPlatform EditorSettingsConfigLib
            Config KP::ImGui)

add_subdirectory(component)
add_subdirectory(api)


add_library(EditorIcons STATIC editor_icons.cpp)
target_include_directories(EditorIcons PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(EditorIcons PUBLIC UIWidgets ImageIO PRIVATE Config KP::NlohmannJson)
