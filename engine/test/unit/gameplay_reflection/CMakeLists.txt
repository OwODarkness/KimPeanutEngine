add_executable(GameplayReflectionUnitTest
    gameplay_reflection_test.cpp
)

target_include_directories(GameplayReflectionUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
    ${CMAKE_SOURCE_DIR}/engine/runtime/core
)

target_link_libraries(GameplayReflectionUnitTest PRIVATE
    GameplayReflection
    KP::GoogleTest
)

include(GoogleTest)
gtest_discover_tests(GameplayReflectionUnitTest)
