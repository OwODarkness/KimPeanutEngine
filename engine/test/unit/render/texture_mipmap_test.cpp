#include <gtest/gtest.h>

#include <initializer_list>

#include "data/texture_mipmap.h"

namespace
{
    kpengine::data::TextureData MakeRgba8(std::initializer_list<uint8_t> pixels)
    {
        kpengine::data::TextureData texture{};
        texture.width = 2;
        texture.height = 2;
        texture.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        texture.pixels = pixels;
        return texture;
    }
}

TEST(TextureMipmaps, BuildsBoundedCompleteChain)
{
    auto texture = MakeRgba8({0, 0, 0, 255,
                               255, 255, 255, 255,
                               0, 0, 0, 255,
                               255, 255, 255, 255});

    ASSERT_TRUE(kpengine::data::GenerateTextureMipChain(
        texture, kpengine::data::TextureSemantic::Color,
        {2, 0}));
    ASSERT_EQ(texture.GetMipLevelCount(), 2U);
    ASSERT_EQ(texture.mip_subresources[0].width, 1U);
    ASSERT_EQ(texture.mip_subresources[0].height, 1U);
    EXPECT_GT(texture.mip_subresources[0].pixels[0], 127U);
    EXPECT_EQ(texture.GetTotalByteCount(), 20U);
}

TEST(TextureMipmaps, NormalMipsRenormalizeTheVector)
{
    auto texture = MakeRgba8({255, 128, 128, 255,
                               128, 255, 128, 255,
                               128, 128, 255, 255,
                               128, 128, 128, 255});

    ASSERT_TRUE(kpengine::data::GenerateTextureMipChain(
        texture, kpengine::data::TextureSemantic::Normal, {2, 0}));
    const auto &mip = texture.mip_subresources[0].pixels;
    const float x = mip[0] / 255.0f * 2.0f - 1.0f;
    const float y = mip[1] / 255.0f * 2.0f - 1.0f;
    const float z = mip[2] / 255.0f * 2.0f - 1.0f;
    EXPECT_NEAR(x * x + y * y + z * z, 1.0f, 0.02f);
}

TEST(TextureMipmaps, PackedLinearChannelsUseLinearAverages)
{
    auto texture = MakeRgba8({0, 10, 20, 30,
                               100, 110, 120, 130,
                               200, 210, 220, 230,
                               255, 245, 235, 225});
    texture.format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;

    ASSERT_TRUE(kpengine::data::GenerateTextureMipChain(
        texture, kpengine::data::TextureSemantic::PackedLinear, {2, 0}));
    const auto &mip = texture.mip_subresources[0].pixels;
    EXPECT_EQ(mip[0], 139U);
    EXPECT_EQ(mip[1], 144U);
    EXPECT_EQ(mip[2], 149U);
    EXPECT_EQ(mip[3], 154U);
}

TEST(TextureMipmaps, OpacityMipsRetainCoveredTexels)
{
    auto texture = MakeRgba8({255, 255, 255, 0,
                               255, 255, 255, 0,
                               255, 255, 255, 0,
                               255, 255, 255, 255});

    ASSERT_TRUE(kpengine::data::GenerateTextureMipChain(
        texture, kpengine::data::TextureSemantic::OpacityMask, {2, 0}));
    EXPECT_GE(texture.mip_subresources[0].pixels[3], 128U);
}

TEST(TextureMipmaps, ClassifiesLooseTextureSemantics)
{
    EXPECT_EQ(kpengine::data::ClassifyTextureSemantic("brick_normal.png"),
              kpengine::data::TextureSemantic::Normal);
    EXPECT_EQ(kpengine::data::ClassifyTextureSemantic("brick_roughness.png"),
              kpengine::data::TextureSemantic::PackedLinear);
    EXPECT_EQ(kpengine::data::ClassifyTextureSemantic("fence_alpha.png"),
              kpengine::data::TextureSemantic::OpacityMask);
    EXPECT_EQ(kpengine::data::ClassifyTextureSemantic("brick_albedo.png"),
              kpengine::data::TextureSemantic::Color);
}
