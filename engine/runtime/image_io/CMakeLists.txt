add_library(ImageIO STATIC
    image_io.cpp
    stb_image_codec.cpp
)

target_include_directories(ImageIO PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ImageIO owns its codec dependency; Asset and Runtime consume only its CPU
# image contract and do not include stb headers directly.
target_link_libraries(ImageIO PUBLIC Base PRIVATE KP::StbImage)
