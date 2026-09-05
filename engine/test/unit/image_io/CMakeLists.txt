add_executable(ImageIOUnitTest
    image_io_test.cpp
)

target_link_libraries(ImageIOUnitTest PRIVATE ImageIO KP::GoogleTest)

include(GoogleTest)
gtest_discover_tests(ImageIOUnitTest)
