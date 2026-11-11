add_library(EditorWorkspaceLib STATIC
    editor_workspace.cpp
)
target_include_directories(EditorWorkspaceLib PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(EditorWorkspaceLib PUBLIC UIWidgets PRIVATE EditorWorkspaceModelLib)

add_library(EditorWorkspaceModelLib STATIC
    editor_workspace_model.cpp
)
target_include_directories(EditorWorkspaceModelLib PUBLIC "${CMAKE_SOURCE_DIR}/engine")
