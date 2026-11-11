target_sources(EditorUILib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_extension_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor_panel_command_provider.cpp
)

add_subdirectory(component)
add_subdirectory(api)


add_library(EditorIcons STATIC editor_icons.cpp)
target_include_directories(EditorIcons PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(EditorIcons PUBLIC UIWidgets ImageIO PRIVATE Config KP::NlohmannJson)
