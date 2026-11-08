add_library(Signal STATIC
    dft.cpp
    fft.cpp
    signal_generator.cpp
    signal_mixer.cpp
    signal_statistics.cpp
    spectrum.cpp)
target_include_directories(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_sources(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/dft.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/fft.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/sample_block.h")
target_sources(Signal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/signal_generator.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/signal_mixer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/signal_statistics.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/spectrum.h")
