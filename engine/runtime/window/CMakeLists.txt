add_library(Window STATIC window_system.cpp glfw_window_system.cpp)

target_include_directories(Window PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/..
 ${CMAKE_CURRENT_SOURCE_DIR}/../..
 
 )
target_link_libraries(Window PUBLIC Core PRIVATE KP::Glfw KP::StbImage KP::Glad user32 gdi32)

# The command adapter is a separate library so Window itself keeps no command
# dependency; RuntimeLib links both. RuntimeCommand is defined by a sibling
# subdirectory added later in runtime/CMakeLists.txt, which CMake resolves at
# generate time.
add_library(RuntimeWindowCommand STATIC window_command_provider.cpp)

target_include_directories(RuntimeWindowCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RuntimeWindowCommand PUBLIC RuntimeCommand Window)
