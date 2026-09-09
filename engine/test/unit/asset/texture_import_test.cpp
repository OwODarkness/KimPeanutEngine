#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

#include "asset/native_texture.h"
#include "asset/texture_importer.h"
#include "data/texture_mipmap.h"

namespace
{
    kpengine::image_io::ImageBuffer MakeImage()
    {
        kpengine::image_io::ImageBuffer image{};
        image.width = 4;
        image.height = 2;
        image.format = kpengine::image_io::ImagePixelFormat::Rgba8;
        image.pixels.resize(4u * 2u * 4u, 255);
        return image;
    }
}

TEST(TextureImportTest, ImportAndCookAreIndependentAndRoundTripNativeMips)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kpengine_texture_import_test.png";
    const auto encoded = kpengine::image_io::EncodePngMemory(MakeImage());
    ASSERT_TRUE(encoded.result.success);
    {
        std::ofstream file(root, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(encoded.bytes.data()),
                   static_cast<std::streamsize>(encoded.bytes.size()));
        ASSERT_TRUE(file.good());
    }

    kpengine::asset::TextureImportRequest request{};
    request.source_path = root;
    request.settings.semantic = kpengine::data::TextureSemantic::Color;
    request.settings.max_dimension = 2;
    const kpengine::asset::ImportedTexture imported =
        kpengine::asset::TextureImporter{}.Import(request);
    const kpengine::asset::CookedTexture cooked =
        kpengine::asset::TextureCooker{}.Cook(imported);
    EXPECT_NO_THROW(kpengine::asset::ValidateNativeTextureProductStructure(cooked.bytes));
    const kpengine::asset::NativeTextureProduct product =
        kpengine::asset::DeserializeNativeTexture(cooked.bytes);

    EXPECT_EQ(imported.image.width, 4u);
    EXPECT_EQ(product.data.width, 2u);
    EXPECT_EQ(product.data.height, 1u);
    EXPECT_EQ(product.data.GetMipLevelCount(), 2u);
    EXPECT_EQ(cooked.product_hash, kpengine::asset::Sha256(cooked.bytes));

    std::error_code error;
    std::filesystem::remove(root, error);
}

TEST(TextureImportTest, CooksSemanticBlockFormatsWithValidatedMipSizes)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Normal;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::PreferBlockCompression;

    const kpengine::asset::CookedTexture cooked = kpengine::asset::TextureCooker{}.Cook(source);
    EXPECT_EQ(cooked.data.format, TextureFormat::TEXTURE_FORMAT_BC5_UNORM);
    EXPECT_EQ(cooked.data.pixels.size(),
              kpengine::data::GetTextureMipByteCount(cooked.data.width, cooked.data.height,
                                                      cooked.data.format));
    EXPECT_TRUE(kpengine::data::IsTextureMipChainValid(cooked.data));

    const kpengine::asset::NativeTextureProduct product =
        kpengine::asset::DeserializeNativeTexture(cooked.bytes);
    EXPECT_EQ(product.data.format, TextureFormat::TEXTURE_FORMAT_BC5_UNORM);
    EXPECT_EQ(product.data.GetTotalByteCount(), cooked.data.GetTotalByteCount());

    source.settings.semantic = kpengine::data::TextureSemantic::OpacityMask;
    EXPECT_EQ(kpengine::asset::TextureCooker{}.Cook(source).data.format,
              TextureFormat::TEXTURE_FORMAT_BC4_UNORM);
    source.settings.semantic = kpengine::data::TextureSemantic::PackedLinear;
    EXPECT_EQ(kpengine::asset::TextureCooker{}.Cook(source).data.format,
              TextureFormat::TEXTURE_FORMAT_BC3_UNORM);
}

TEST(TextureImportTest, RequiredBlockCompressionIsDeterministic)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Color;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;
    const kpengine::asset::CookedTexture first = kpengine::asset::TextureCooker{}.Cook(source);
    const kpengine::asset::CookedTexture second = kpengine::asset::TextureCooker{}.Cook(source);
    EXPECT_EQ(first.data.format, TextureFormat::TEXTURE_FORMAT_BC3_SRGB);
    EXPECT_EQ(first.bytes, second.bytes);
    EXPECT_EQ(first.product_hash, second.product_hash);
}

TEST(TextureImportTest, Bc4OpacityUsesRedChannelAndAllInterpolatedEntries)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.image.height = 4;
    source.image.pixels.resize(4U * 4U * 4U, 255);
    for (std::size_t index = 0; index < 16; ++index)
    {
        source.image.pixels[index * 4U + 0U] = 0;
        source.image.pixels[index * 4U + 3U] = 128;
    }
    source.image.pixels[0] = 255;
    source.image.pixels[4] = 0;
    source.image.pixels[8] = 37;
    source.settings.semantic = kpengine::data::TextureSemantic::OpacityMask;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;

    const kpengine::asset::CookedTexture cooked = kpengine::asset::TextureCooker{}.Cook(source);
    ASSERT_EQ(cooked.data.format, TextureFormat::TEXTURE_FORMAT_BC4_UNORM);
    ASSERT_GE(cooked.data.pixels.size(), 8U);
    EXPECT_EQ(cooked.data.pixels[0], 255U);
    EXPECT_EQ(cooked.data.pixels[1], 0U);

    std::uint64_t indices = 0;
    for (std::size_t index = 0; index < 6; ++index)
    {
        indices |= static_cast<std::uint64_t>(cooked.data.pixels[2U + index]) << (index * 8U);
    }
    EXPECT_EQ((indices >> 6U) & 0x7U, 7U);
}
