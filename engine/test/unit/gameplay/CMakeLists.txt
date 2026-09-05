add_executable(GameplayUnitTest
    gameplay_world_test.cpp
)

target_include_directories(GameplayUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(GameplayUnitTest PRIVATE Gameplay KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(GameplayUnitTest)
