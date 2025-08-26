add_library(System 
    asset_system.cpp
    world_system.cpp
    )

target_include_directories(System PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(System PUBLIC Input PRIVATE Math Log RuntimeLib)