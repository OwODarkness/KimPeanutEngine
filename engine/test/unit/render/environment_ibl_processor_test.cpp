#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "resource/environment_ibl_processor.h"

namespace
{
    kpengine::data::TextureData MakeConstantHdr(uint16_t red, uint16_t green,
                                                uint16_t blue)
    {
        kpengine::data::TextureData result{};
        result.width = 4;
        result.height = 2;
        result.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
        result.pixels.resize(4u * 2u * 4u * sizeof(uint16_t));
        const std::array<uint16_t, 4> pixel{red, green, blue, 0x3c00u};
        for (size_t offset = 0; offset < result.pixels.size();
             offset += pixel.size() * sizeof(uint16_t))
        {
            std::memcpy(result.pixels.data() + offset, pixel.data(),
                        pixel.size() * sizeof(uint16_t));
        }
        return result;
    }

    float DecodePositiveHalf(const kpengine::data::TextureData &texture,
                             size_t pixel_index, size_t channel)
    {
        uint16_t value = 0;
        const size_t offset = (pixel_index * 4 + channel) * sizeof(uint16_t);
        std::memcpy(&value, texture.pixels.data() + offset, sizeof(value));
        const uint32_t exponent = (value >> 10) & 0x1fu;
        const uint32_t mantissa = value & 0x03ffu;
        if (exponent == 0)
        {
            return std::ldexp(static_cast<float>(mantissa), -24);
        }
        return std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                          static_cast<int>(exponent) - 15);
    }
}

TEST(EnvironmentIblProcessorTest, ConstantPanoramaProducesStableDerivedLighting)
{
    const kpengine::data::TextureData source =
        MakeConstantHdr(0x3c00u, 0x3800u, 0x3400u); // 1.0, 0.5, 0.25
    kpengine::resource::EnvironmentIblSettings settings{};
    settings.irradiance_width = 4;
    settings.irradiance_height = 2;
    settings.prefilter_width = 4;
    settings.prefilter_height = 2;
    settings.prefilter_level_count = 3;
    settings.brdf_lut_size = 4;
    settings.sample_count = 32;

    const auto result = kpengine::resource::BuildEnvironmentIbl(source, settings);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->prefiltered_radiance.height, 6u);
    EXPECT_EQ(result->prefilter_level_count, 3u);
    EXPECT_NEAR(DecodePositiveHalf(result->irradiance, 0, 0), 3.14159f, 0.01f);
    EXPECT_NEAR(DecodePositiveHalf(result->irradiance, 0, 1), 1.5708f, 0.01f);
    EXPECT_NEAR(DecodePositiveHalf(result->prefiltered_radiance, 0, 0), 1.0f, 0.01f);
    EXPECT_NEAR(DecodePositiveHalf(result->prefiltered_radiance, 0, 2), 0.25f, 0.01f);
    EXPECT_TRUE(std::isfinite(DecodePositiveHalf(result->brdf_lut, 0, 0)));
    EXPECT_GE(DecodePositiveHalf(result->brdf_lut, 0, 0), 0.0f);
}

TEST(EnvironmentIblProcessorTest, RejectsNonHdrSource)
{
    kpengine::data::TextureData source{};
    source.width = 1;
    source.height = 1;
    source.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
    source.pixels = {255, 255, 255, 255};

    EXPECT_FALSE(kpengine::resource::BuildEnvironmentIbl(source).has_value());
}
