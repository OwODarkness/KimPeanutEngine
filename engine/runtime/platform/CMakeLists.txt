add_library(Platform STATIC memory_stats_sampler.cpp win/windows_memory_stats_sampler.cpp)

target_include_directories(Platform PUBLIC
 ${CMAKE_CURRENT_SOURCE_DIR}/..
 ${CMAKE_CURRENT_SOURCE_DIR}/../..
 )

target_link_libraries(Platform PUBLIC Core PRIVATE psapi)
