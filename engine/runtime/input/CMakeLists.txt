cmake_minimum_required(VERSION 3.5.0)

add_library(Input STATIC
    input_context.cpp 
    input_system.cpp
)

target_include_directories(Input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Input PUBLIC Core)