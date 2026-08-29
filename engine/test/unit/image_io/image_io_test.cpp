#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "image_io/image_io.h"

namespace
{
    std::filesystem::path MakeTemporaryPngPath()
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
            ("kpengine_image_io_" + std::to_string(tick) + ".png");
    }

    kpengine::image_io::ImageBuffer MakeTestImage()
    {
        kpengine::image_io::ImageBuffer image;
        image.width = 2;
        image.height = 2;
        image.pixels = {
            255, 0, 0, 255,      0, 255, 0, 255,
            0, 0, 255, 255,      255, 255, 255, 255,
        };
        return image;
    }
}

TEST(ImageIOTest, ValidatesRgba8ByteCount)
{
    auto image = MakeTestImage();
    EXPECT_EQ(image.ExpectedByteCount(), 16u);
    EXPECT_TRUE(image.IsValid());

    image.pixels.pop_back();
    EXPECT_FALSE(image.IsValid());
}

TEST(ImageIOTest, WritesAndDecodesLosslessPng)
{
    const std::filesystem::path output = MakeTemporaryPngPath();
    const auto cleanup = [&output]()
    {
        std::error_code error;
        std::filesystem::remove(output, error);
    };

    const auto image = MakeTestImage();
    const auto write_result = kpengine::image_io::WritePng(image, output.string());
    ASSERT_TRUE(write_result.success) << write_result.diagnostic;

    const auto decoded = kpengine::image_io::DecodeImageFile(output.string());
    cleanup();
    ASSERT_TRUE(decoded.result.success) << decoded.result.diagnostic;
    EXPECT_EQ(decoded.image.width, image.width);
    EXPECT_EQ(decoded.image.height, image.height);
    EXPECT_EQ(decoded.image.pixels, image.pixels);
}

TEST(ImageIOTest, RejectsInvalidOutputImage)
{
    kpengine::image_io::ImageBuffer invalid;
    invalid.width = 1;
    invalid.height = 1;
    invalid.pixels = {0, 0, 0};

    const auto result = kpengine::image_io::WritePng(invalid, "unused.png");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.diagnostic.empty());
}
