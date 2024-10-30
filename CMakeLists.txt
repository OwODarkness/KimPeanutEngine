cmake_minimum_required(VERSION 3.5.0)
project(KimPeanutEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(
    third_party
    )

add_subdirectory(engine)
add_subdirectory(third_party)

add_executable(KimPeanutEngine 
    engine/editor/main.cpp
)
target_link_libraries(KimPeanutEngine 
    EngineLib
    ThirdPartyLib
)
