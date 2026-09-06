add_executable(KimPeanutAssetTool
    asset_tool_main.cpp
)

target_link_libraries(KimPeanutAssetTool PRIVATE AssetImport)
copy_runtime_dependencies(KimPeanutAssetTool)
