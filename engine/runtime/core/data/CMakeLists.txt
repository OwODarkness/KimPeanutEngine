add_library(Data INTERFACE)

target_include_directories(Data INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
# Correct: INTERFACE
target_link_libraries(Data INTERFACE Math)