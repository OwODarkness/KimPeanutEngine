#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "asset/asset_manager.h"
#include "asset/asset_import_registry.h"
#include "config/path.h"
#include "live2d_import.h"
#include "live2d_model_resource.h"
#include "live2d_system.h"
#include "live2d_product.h"
#include "live2d_registration.h"

namespace
{
    constexpr const char *kModelPath =
        "live2d/hiyori_pro/runtime/hiyori_pro_t11.model3.json";

    std::filesystem::path MakeTempContentRoot()
    {
        return std::filesystem::temp_directory_path() /
               "kpengine_live2d_hiyori_test_root";
    }
}

TEST(Live2DAssetTest, ImportsCheckedInModel3PackageDeterministically)
{
    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = kpengine::project_root / "asset";
    request.archive_root = MakeTempContentRoot() / ".archive";
    std::error_code cleanup_error;
    std::filesystem::remove_all(request.archive_root, cleanup_error);
    request.source_path = kModelPath;

    const auto first = registry.Execute(request, {}, diagnostic);
    ASSERT_TRUE(diagnostic.empty()) << diagnostic;
    ASSERT_NE(first.product, nullptr) << first.diagnostic;
    const auto first_product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>(first.product);
    ASSERT_NE(first_product, nullptr);
    EXPECT_EQ(first_product->value.product.textures.size(), 2u);
    EXPECT_FALSE(first_product->value.product.moc_bytes.empty());

    const auto second = registry.Execute(request, {}, diagnostic);
    ASSERT_TRUE(diagnostic.empty()) << diagnostic;
    const auto second_product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>(second.product);
    ASSERT_NE(second_product, nullptr);
    EXPECT_EQ(first_product->value.product_bytes, second_product->value.product_bytes);
    EXPECT_EQ(first_product->value.product.textures[0].path.size() > 9u, true);
    EXPECT_EQ(first_product->value.product.textures[0].path.substr(0, 9), ".archive/");

    std::error_code cleanup_after_test;
    std::filesystem::remove_all(request.archive_root.parent_path(), cleanup_after_test);
}

TEST(Live2DAssetTest, RejectsSourcePathEscapeAndGenericJson)
{
    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic));
    ASSERT_TRUE(registry.Seal(diagnostic));

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = kpengine::project_root / "asset";
    request.archive_root = MakeTempContentRoot() / ".archive";
    std::error_code cleanup_error;
    std::filesystem::remove_all(request.archive_root, cleanup_error);
    request.source_path = "../outside.model3.json";
    const auto escaped = registry.Execute(request, {}, diagnostic);
    EXPECT_EQ(escaped.product, nullptr);
    EXPECT_FALSE(escaped.diagnostic.empty());

    diagnostic.clear();
    request.source_path = "live2d/hiyori_pro/runtime/not-a-model.json";
    EXPECT_EQ(registry.Resolve(request.source_path, {}, diagnostic), nullptr);
}

TEST(Live2DAssetTest, LoadsImportedProductThroughAssetManagerDependencies)
{
    kpengine::asset::AssetManager &manager = kpengine::asset::AssetManager::GetInstance();
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DAssetTypes(manager, diagnostic))
        << diagnostic;

    kpengine::asset::ImportProviderRegistry registry;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = kpengine::project_root / "asset";
    const std::filesystem::path content_root = MakeTempContentRoot();
    const std::filesystem::path archive_root = content_root / ".archive";
    std::error_code cleanup_error;
    std::filesystem::remove_all(archive_root, cleanup_error);
    request.archive_root = archive_root;
    request.source_path = kModelPath;
    const auto imported = registry.Execute(request, {}, diagnostic);
    ASSERT_TRUE(diagnostic.empty()) << diagnostic;
    const auto product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>(imported.product);
    ASSERT_NE(product, nullptr) << imported.diagnostic;

    const std::filesystem::path output = content_root / "hiyori.live2d";
    std::filesystem::create_directories(content_root);
    {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(product->value.product_bytes.data()),
                   static_cast<std::streamsize>(product->value.product_bytes.size()));
        ASSERT_TRUE(file.good());
    }

    const kpengine::asset::AssetID id = manager.LoadSync(output.generic_string());
    ASSERT_TRUE(id.IsValid());
    EXPECT_EQ(id.type, kpengine::live2d::kLive2DModelAssetType);
    const auto resource = manager.GetResource<kpengine::live2d::Live2DModelResource>(id);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->Product().textures.size(), 2u);
    EXPECT_EQ(manager.GetAsset(id)->GetDependencies().size(), 2u);

    kpengine::live2d::Live2DSystem system;
    ASSERT_TRUE(system.Initialize());
    auto first_instance = system.CreateInstance(id);
    auto second_instance = system.CreateInstance(id);
    ASSERT_NE(first_instance, nullptr);
    ASSERT_NE(second_instance, nullptr);
    ASSERT_TRUE(first_instance->IsValid());
    ASSERT_TRUE(second_instance->IsValid());
    EXPECT_EQ(&first_instance->Resource(), &second_instance->Resource());
    ASSERT_EQ(first_instance->TextureDependencies().size(), 2u);
    ASSERT_EQ(second_instance->TextureDependencies().size(), 2u);
    EXPECT_EQ(first_instance->TextureDependencies()[0].get(),
              second_instance->TextureDependencies()[0].get());
    EXPECT_EQ(first_instance->TextureDependencies()[1].get(),
              second_instance->TextureDependencies()[1].get());

    kpengine::live2d::Live2DStaticModelData static_data;
    std::string extraction_diagnostic;
    ASSERT_TRUE(first_instance->ExtractStaticData(static_data,
                                                   extraction_diagnostic))
        << extraction_diagnostic;
    ASSERT_FALSE(static_data.drawables.empty());
    EXPECT_EQ(static_data.feature_report.drawable_count,
              static_data.drawables.size());
    EXPECT_EQ(static_data.maximum_position_bytes,
              static_data.uvs.size() * sizeof(kpengine::live2d::Live2DVector2));

    kpengine::live2d::Live2DStaticModelData second_static_data;
    ASSERT_TRUE(second_instance->ExtractStaticData(second_static_data,
                                                   extraction_diagnostic))
        << extraction_diagnostic;
    EXPECT_EQ(static_data.topology_revision, second_static_data.topology_revision);
    EXPECT_EQ(static_data.uvs.size(), second_static_data.uvs.size());
    EXPECT_EQ(static_data.indices, second_static_data.indices);

    kpengine::live2d::Live2DFrameSnapshot first_frame;
    kpengine::live2d::Live2DFrameSnapshot second_frame;
    ASSERT_TRUE(first_instance->ExtractFrameSnapshot(first_frame,
                                                     extraction_diagnostic))
        << extraction_diagnostic;
    ASSERT_TRUE(second_instance->ExtractFrameSnapshot(second_frame,
                                                      extraction_diagnostic))
        << extraction_diagnostic;
    EXPECT_EQ(first_frame.frame_sequence, 1u);
    EXPECT_EQ(second_frame.frame_sequence, 1u);
    EXPECT_EQ(first_frame.positions.size(), second_frame.positions.size());
    ASSERT_EQ(first_frame.positions.size(), static_data.uvs.size());
    for (std::size_t vertex = 0u; vertex < first_frame.positions.size(); ++vertex)
    {
        EXPECT_FLOAT_EQ(first_frame.positions[vertex].x,
                        second_frame.positions[vertex].x);
        EXPECT_FLOAT_EQ(first_frame.positions[vertex].y,
                        second_frame.positions[vertex].y);
    }

    ASSERT_GT(first_instance->ParameterCount(), 0u);

    float first_before = 0.0f;
    float second_before = 0.0f;
    ASSERT_TRUE(first_instance->GetParameterValue(0, first_before));
    ASSERT_TRUE(second_instance->GetParameterValue(0, second_before));
    float minimum = 0.0f;
    float maximum = 0.0f;
    ASSERT_TRUE(first_instance->GetParameterRange(0, minimum, maximum));
    const float changed_value = first_before == minimum ? maximum : minimum;
    ASSERT_NE(changed_value, first_before);
    ASSERT_TRUE(first_instance->SetParameterValue(0, changed_value));
    ASSERT_TRUE(first_instance->Update());
    kpengine::live2d::Live2DFrameSnapshot changed_frame;
    ASSERT_TRUE(first_instance->ExtractFrameSnapshot(changed_frame,
                                                     extraction_diagnostic))
        << extraction_diagnostic;
    EXPECT_EQ(changed_frame.frame_sequence, 2u);
    bool position_changed = false;
    for (std::size_t vertex = 0u;
         vertex < changed_frame.positions.size(); ++vertex)
    {
        if (changed_frame.positions[vertex].x != first_frame.positions[vertex].x ||
            changed_frame.positions[vertex].y != first_frame.positions[vertex].y)
        {
            position_changed = true;
            break;
        }
    }
    EXPECT_TRUE(position_changed);
    float first_after = 0.0f;
    float second_after = 0.0f;
    ASSERT_TRUE(first_instance->GetParameterValue(0, first_after));
    ASSERT_TRUE(second_instance->GetParameterValue(0, second_after));
    EXPECT_FLOAT_EQ(first_after, changed_value);
    EXPECT_FLOAT_EQ(second_after, second_before);

    manager.UnRegisterAsset(id);
    EXPECT_EQ(manager.GetLiveAssetCount(kpengine::live2d::kLive2DModelAssetType), 0u);
    EXPECT_TRUE(first_instance->IsValid());
    EXPECT_TRUE(second_instance->IsValid());
    EXPECT_TRUE(second_instance->ExtractFrameSnapshot(second_frame,
                                                      extraction_diagnostic))
        << extraction_diagnostic;
    EXPECT_EQ(second_frame.frame_sequence, 2u);

    second_instance.reset();
    first_instance.reset();
    system.Shutdown();
    EXPECT_FALSE(system.IsInitialized());

    std::vector<std::byte> malformed = product->value.product_bytes;
    malformed[0] = static_cast<std::byte>('X');
    {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(malformed.data()),
                   static_cast<std::streamsize>(malformed.size()));
        ASSERT_TRUE(file.good());
    }
    EXPECT_FALSE(manager.LoadSync(output.generic_string()).IsValid());
    EXPECT_EQ(manager.GetLiveAssetCount(kpengine::live2d::kLive2DModelAssetType), 0u);

    std::error_code error;
    std::filesystem::remove_all(content_root, error);
}
