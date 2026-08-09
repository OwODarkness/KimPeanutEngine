cmake_minimum_required(VERSION 3.5.0)
project(KimPeanutEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)


list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

include(RuntimeDependencies)

# include_directories(
#     third_party
#     third_party/googletest/googletest/include
#     third_party/vulkan/
#     )
add_subdirectory(third_party)

add_subdirectory(engine)



add_executable(KimPeanutEngine 
    engine/editor/main.cpp
)

if(MSVC)
    target_compile_options(KimPeanutEngine PRIVATE /utf-8)
endif()


target_link_libraries(KimPeanutEngine 
    EngineLib
    ThirdPartyLib
)

copy_thirdparty_dlls(KimPeanutEngine)

#TEST
add_subdirectory(engine/test)
