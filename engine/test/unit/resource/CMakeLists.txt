add_executable(ResourceUnitTest resource_test.cpp)
target_link_libraries(ResourceUnitTest PRIVATE Resource KP::GoogleTest)
include(GoogleTest)
gtest_discover_tests(ResourceUnitTest PROPERTIES LABELS "resource")
