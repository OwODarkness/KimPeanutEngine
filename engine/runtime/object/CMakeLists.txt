cmake_minimum_required(VERSION 3.5.0)

add_library(Object STATIC
    object.cpp
)

target_include_directories(Object PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Object PUBLIC Core)