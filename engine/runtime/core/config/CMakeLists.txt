# Generate global.h in the build folder
set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})
set(PROJECT_BINARY_DIR ${CMAKE_BINARY_DIR})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/global.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/global.h
)

# Create interface library
add_library(Config INTERFACE)

target_include_directories(Config INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
)
