add_library(Database STATIC
    database.cpp
)

target_include_directories(Database PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Database PRIVATE KP::SQLite)
