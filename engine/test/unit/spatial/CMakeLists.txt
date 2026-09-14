add_executable(
    SpatialUnitTest
    linear_bvh_test.cpp
)

target_link_libraries(SpatialUnitTest PRIVATE Spatial KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(SpatialUnitTest)
