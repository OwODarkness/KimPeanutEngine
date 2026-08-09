add_library(Math STATIC math.cpp )

target_include_directories(Math INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

