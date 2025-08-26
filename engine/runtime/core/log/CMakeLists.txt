add_library(Log STATIC
    logger.cpp 
    log_system.cpp
)

target_include_directories(Log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(Log PRIVATE Config)