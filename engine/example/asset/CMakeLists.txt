add_library(AssetExample STATIC
asset_example.cpp)

target_include_directories(AssetExample PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(AssetExample
PRIVATE
Asset
Config
)


