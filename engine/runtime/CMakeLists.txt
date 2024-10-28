file(GLOB RUNTIME_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB RUNTIME_LOG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/log/*.cpp")
#file(GLOB RUNTIME_MATH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/math/*.cpp")
add_library(RuntimeLib STATIC ${RUNTIME_SRC} ${RUNTIME_LOG_SRC} )

target_include_directories(RuntimeLib PUBLIC
"${CMAKE_CURRENT_SOURCE_DIR}/core/log/"
)