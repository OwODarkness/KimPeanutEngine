file(GLOB PATH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/path/*.cpp")


set(PROJECT_ROOT_DIR "${CMAKE_SOURCE_DIR}")

configure_file(
    "${CMAKE_SOURCE_DIR}/engine/platform/global.h.in" 
    "${CMAKE_SOURCE_DIR}/engine/platform/global.h" 
)

add_library(PlatformLib STATIC
${PATH_SRC}
) 