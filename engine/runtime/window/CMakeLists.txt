add_library(Window STATIC window_system.cpp glfw_window_system.cpp)

target_include_directories(Window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Window PUBLIC Core)