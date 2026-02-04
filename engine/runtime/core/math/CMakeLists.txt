# runtime/core/math/CMakeLists.txt
add_library(Math STATIC math.cpp)

# expose 'core' so that 'math/math_header.h' works
target_include_directories(Math INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

