add_library(AudioPlayerController STATIC audio_player_controller.cpp)
target_include_directories(AudioPlayerController PUBLIC "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerController PUBLIC Audio PRIVATE Asset AssetImport Log Config)

add_subdirectory(editor)

add_library(AudioPlayerMode STATIC
    audio_player_host.cpp)

target_include_directories(AudioPlayerMode PUBLIC
    "${CMAKE_SOURCE_DIR}/engine")

target_link_libraries(AudioPlayerMode
    PUBLIC Audio RuntimeLib
    PRIVATE AudioPlayerController AudioPlayerEditorUI)

add_library(AudioPlayerRegistration OBJECT
    audio_player_registration.cpp)
target_include_directories(AudioPlayerRegistration PRIVATE
    "${CMAKE_SOURCE_DIR}/engine")
target_link_libraries(AudioPlayerRegistration PRIVATE AudioPlayerMode ModuleBootstrap)
