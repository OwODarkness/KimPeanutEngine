# EditorLib — the tool layer. One cohesive library: this file owns the target and
# the shared config, and each subdirectory's CMakeLists.txt explicitly lists its
# own sources via target_sources (no GLOB, so a stray .cpp can't silently join the
# build). Headers sit beside their sources and are included via the engine root
# ("editor/..." paths), matching the runtime convention. main.cpp is the exe entry
# (root CMakeLists.txt) and deliberately stays out of the lib.
add_library(EditorLib STATIC
    editor.cpp
)

add_subdirectory(context)
add_subdirectory(ui)
add_subdirectory(platform)
add_subdirectory(log)
add_subdirectory(settings)

target_include_directories(EditorLib PUBLIC
    ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(EditorLib PRIVATE RuntimeLib imgui glad nlohmann)
