add_library(Signal STATIC
    signal_generator.cpp
    signal_statistics.cpp)
target_include_directories(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_sources(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sample_block.h")
target_sources(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/signal_generator.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/signal_statistics.h")
