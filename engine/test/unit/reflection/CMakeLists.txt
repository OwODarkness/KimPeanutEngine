add_executable(ReflectionUnitTest
    reflection_registry_test.cpp
)

target_link_libraries(ReflectionUnitTest PRIVATE
    Reflection
    EnTT::EnTT
    KP::GoogleTest
)

target_include_directories(ReflectionUnitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/runtime
)

include(GoogleTest)
gtest_discover_tests(ReflectionUnitTest)
