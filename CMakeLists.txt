cmake_minimum_required(VERSION 3.5.0)
project(KimPeanutEngine VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

include(RuntimeDependencies)


add_subdirectory(third_party)

add_subdirectory(engine)



add_executable(KimPeanutEngine 
    engine/editor/main.cpp
)

if(MSVC)
    target_compile_options(KimPeanutEngine PRIVATE /utf-8)
    # Keep the optimized editor GUI visible without creating a console window.
    set_property(TARGET KimPeanutEngine PROPERTY WIN32_EXECUTABLE
        "$<CONFIG:RelWithDebInfo>")
    set_property(TARGET KimPeanutEngine APPEND_STRING PROPERTY LINK_FLAGS_RELWITHDEBINFO
        " /ENTRY:mainCRTStartup")
endif()


target_link_libraries(KimPeanutEngine 
    EngineLib
)

#TEST
add_subdirectory(engine/test)
