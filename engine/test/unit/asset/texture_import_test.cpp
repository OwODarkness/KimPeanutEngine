#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "asset/native_texture.h"
#include "asset/texture_importer.h"

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

TEST(TextureImportTest, RejectsUnavailableBlockCompressionInsteadOfSilentlyFallingBack)
{
    kpengine::asset::ImportedTexture source{};
    source.image = MakeImage();
    source.settings.semantic = kpengine::data::TextureSemantic::Color;
    source.settings.compression =
        kpengine::asset::TextureCompressionPolicy::RequireBlockCompression;
    EXPECT_THROW(kpengine::asset::TextureCooker{}.Cook(source),
                 kpengine::asset::TextureCookError);
}
