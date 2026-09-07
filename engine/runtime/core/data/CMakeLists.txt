add_library(Data STATIC
    texture_mipmap.cpp
)

target_include_directories(Data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(Data INTERFACE Math)
