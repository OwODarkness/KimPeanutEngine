add_executable(Scene3DHostTest
    scene3d_host_test.cpp)

target_link_libraries(Scene3DHostTest PRIVATE
    RuntimeLib KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(Scene3DHostTest PROPERTIES LABELS "scene3d")
