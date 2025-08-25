file(GLOB RUNTIME_SYSTEM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/system/*.cpp")
file(GLOB RUNTIME_UTILITY_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/utility/*.cpp")
file(GLOB RUNTIME_RENDER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp")
file(GLOB RUNTIME_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

add_subdirectory(core)
add_subdirectory(object)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(component)
add_subdirectory(game_framework)

add_library(RuntimeLib STATIC 
    engine.cpp 
    runtime_global_context.cpp
    ${RUNTIME_SYSTEM_SRC}
    ${RUNTIME_UTILITY_SRC}
    ${RUNTIME_RENDER_SRC}
    ${RUNTIME_TEST_SRC}
    )

target_include_directories(RuntimeLib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RuntimeLib PRIVATE Log Object Graphics Input Component GameFramework )
