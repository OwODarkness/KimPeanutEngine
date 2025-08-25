add_library(Log STATIC
    logger.cpp logger.h 
    log_entry.h
)

target_include_directories(Log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Log PRIVATE RuntimeLib)