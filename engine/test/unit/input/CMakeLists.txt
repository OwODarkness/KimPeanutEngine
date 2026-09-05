add_executable(InputSystemTest
    input_system_test.cpp
)

target_include_directories(InputSystemTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

target_link_libraries(InputSystemTest PRIVATE Input KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(InputSystemTest)
