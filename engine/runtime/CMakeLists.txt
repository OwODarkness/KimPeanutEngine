file(GLOB RUNTIME_UTILITY_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/utility/*.cpp")
file(GLOB RUNTIME_TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")

add_subdirectory(core)
add_subdirectory(core/system)
add_subdirectory(object)
add_subdirectory(graphics)
add_subdirectory(input)
add_subdirectory(component)
add_subdirectory(game_framework)
add_subdirectory(render)

add_library(RuntimeLib STATIC 
    engine.cpp 
    runtime_global_context.cpp
    ${RUNTIME_UTILITY_SRC}
    ${RUNTIME_TEST_SRC}
    )

target_include_directories(RuntimeLib PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RuntimeLib  PRIVATE Object Graphics Input Component GameFramework System Render)
