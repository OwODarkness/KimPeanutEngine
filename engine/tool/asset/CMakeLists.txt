add_executable(KimPeanutAssetTool
    asset_tool_main.cpp
)

target_link_libraries(KimPeanutAssetTool PRIVATE AssetImport)

if(TARGET Live2DImport)
    target_link_libraries(KimPeanutAssetTool PRIVATE Live2DImport)
    target_compile_definitions(KimPeanutAssetTool PRIVATE KPENGINE_ASSET_TOOL_HAS_LIVE2D=1)
endif()

copy_runtime_dependencies(KimPeanutAssetTool)
