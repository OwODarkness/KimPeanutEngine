#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "asset/asset_import_adapters.h"
#include "asset/asset_import_registry.h"
#include "image_io/image_io.h"

namespace
{
    kpengine::image_io::ImageBuffer MakeImage()
    {
        kpengine::image_io::ImageBuffer image{};
        image.width = 2;
        image.height = 2;
        image.format = kpengine::image_io::ImagePixelFormat::Rgba8;
        image.pixels.resize(2u * 2u * 4u, 255);
        return image;
    }
}

TEST(AssetImportAdapterTest, RegistersModelAndTextureServicesAndPublishesTextureProduct)
{
    static std::atomic_uint64_t sequence{};
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("kpengine_asset_import_adapter_test_" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    const std::filesystem::path source = root / "textures" / "albedo.png";
    const std::filesystem::path archive = root / ".archive";
    std::filesystem::create_directories(source.parent_path());
    const auto encoded = kpengine::image_io::EncodePngMemory(MakeImage());
    ASSERT_TRUE(encoded.result.success);
    {
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char *>(encoded.bytes.data()),
                     static_cast<std::streamsize>(encoded.bytes.size()));
        ASSERT_TRUE(output.good());
    }

    kpengine::asset::ImportProviderRegistry registry{};
    kpengine::asset::ModelImportService model_service{};
    std::string diagnostic;
    ASSERT_TRUE(kpengine::asset::RegisterModelImportProvider(
                    registry, model_service, {}, diagnostic))
        << diagnostic;
    ASSERT_TRUE(kpengine::asset::RegisterTextureImportProvider(
                    registry, {}, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    const auto *const model_provider =
        registry.Resolve("models/character.obj", {}, diagnostic);
    ASSERT_NE(model_provider, nullptr) << diagnostic;
    EXPECT_EQ(model_provider->id, "assimp");

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root;
    request.archive_root = archive;
    request.source_path = "textures/albedo.png";
    const auto result = registry.Execute(request, {}, diagnostic);
    EXPECT_TRUE(diagnostic.empty());
    EXPECT_EQ(result.provider_id, "image");
    EXPECT_EQ(result.status, kpengine::asset::ImportProviderStatus::Cooked);
    const auto product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<kpengine::asset::CookedTexture,
                                            kpengine::asset::ImportProviderKind::Texture>>(
        result.product);
    ASSERT_NE(product, nullptr);
    const auto product_path = archive / kpengine::asset::ProductRelativePath(
        kpengine::asset::ArchiveProductType::Texture, product->value.product_hash, "texture");
    EXPECT_TRUE(std::filesystem::is_regular_file(product_path));

    std::error_code error;
    std::filesystem::remove_all(root, error);
}
