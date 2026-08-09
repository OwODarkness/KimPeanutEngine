add_library(ExampleLib INTERFACE)

add_subdirectory(audio)
add_subdirectory(graphics)
add_subdirectory(tts)
add_subdirectory(asset)
target_link_libraries(ExampleLib INTERFACE 
AudioExample
GraphicsExample 
GameFramework 
TTSExample
AssetExample
)