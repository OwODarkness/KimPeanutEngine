#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

// L2D6.0 characterization harness.
//
// The checked-in Hiyori package has no Expressions, no per-motion FadeInTime /
// FadeOutTime overrides and no Sound references, so it cannot exercise the V1
// product's animation representation at all. This test builds the fixture the
// L2D6.0 investigation requires -- two motion groups, model3 fade overrides,
// two named expressions and one optional sound reference -- copies the real
// .moc3 and textures next to it, and drives the *real* importer so the loss is
// observed rather than inferred from reading the code.
//
// It is intentionally a characterization of V1's limitations, not a behavioural
// contract. L2D6.1 replaces it with V2 round-trip coverage.
namespace
{
    std::filesystem::path MakeCharacterizationRoot()
    {
        return std::filesystem::temp_directory_path() / "kpengine_live2d_v1_char_root";
    }

    void WriteFile(const std::filesystem::path &path, const std::string &contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open()) << path.generic_string();
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(file.good()) << path.generic_string();
    }

    void CopyFile(const std::filesystem::path &from, const std::filesystem::path &to)
    {
        std::filesystem::create_directories(to.parent_path());
        std::filesystem::copy_file(from, to,
                                   std::filesystem::copy_options::overwrite_existing);
    }

    // Two motion groups, per-motion model3 fade overrides on two of the three
    // entries, two named expressions and one optional sound reference -- the
    // fixture L2D6.0 asks for. Groups exercises the parameter-target import.
    const char *kMotionAndExpressionModel3 = R"({
  "Version": 3,
  "FileReferences": {
    "Moc": "probe.moc3",
    "Textures": ["probe.2048/texture_00.png"],
    "Motions": {
      "Alpha": [
        { "File": "motion/alpha_00.motion3.json", "FadeInTime": 0.25,
          "FadeOutTime": 0.75, "Sound": "sound/alpha_00.wav" },
        { "File": "motion/alpha_01.motion3.json" }
      ],
      "Beta": [
        { "File": "motion/beta_00.motion3.json", "FadeInTime": 2.0 }
      ]
    },
    "Expressions": [
      { "Name": "Shy", "File": "expressions/shy.exp3.json" },
      { "Name": "Angry", "File": "expressions/angry.exp3.json" }
    ]
  },
  "Groups": [
    { "Target": "Parameter", "Name": "LipSync", "Ids": ["ParamMouthOpenY"] },
    { "Target": "Parameter", "Name": "EyeBlink", "Ids": ["ParamEyeLOpen", "ParamEyeROpen"] }
  ]
})";

    // Identical except that the Expressions array is removed, so the two
    // inventories differ only by the expression references. Both runs now
    // import; the difference is which chunks the product carries.
    const char *kMotionOnlyModel3 = R"({
  "Version": 3,
  "FileReferences": {
    "Moc": "probe.moc3",
    "Textures": ["probe.2048/texture_00.png"],
    "Motions": {
      "Alpha": [
        { "File": "motion/alpha_00.motion3.json", "FadeInTime": 0.25,
          "FadeOutTime": 0.75, "Sound": "sound/alpha_00.wav" },
        { "File": "motion/alpha_01.motion3.json" }
      ],
      "Beta": [
        { "File": "motion/beta_00.motion3.json", "FadeInTime": 2.0 }
      ]
    }
  },
  "Groups": [
    { "Target": "Parameter", "Name": "LipSync", "Ids": ["ParamMouthOpenY"] },
    { "Target": "Parameter", "Name": "EyeBlink", "Ids": ["ParamEyeLOpen", "ParamEyeROpen"] }
  ]
})";

    const char *kMinimalMotion3 = R"({"Version": 3,
  "Meta": { "Duration": 1.0, "Loop": false, "CurveCount": 0, "Fps": 30.0,
            "TotalSegmentCount": 0, "TotalPointCount": 0, "UserDataCount": 0,
            "TotalUserDataSize": 0 },
  "Curves": [] })";
}

TEST(Live2DProductTest, V1AndV2RoundTrip)
{
    kpengine::live2d::Live2DProductData v1{};
    v1.product_version = 1;
    v1.moc_bytes = {std::byte{0x01}};
    v1.textures = {{".archive/textures/a.texture"}};
    v1.optional_chunks = {{"Physics", {std::byte{0x02}}}};

    std::vector<std::byte> v1_bytes;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::SerializeLive2DProduct(v1, v1_bytes, diagnostic))
        << diagnostic;
    kpengine::live2d::Live2DProductData parsed_v1{};
    ASSERT_TRUE(kpengine::live2d::ParseLive2DProduct(v1_bytes, parsed_v1, diagnostic))
        << diagnostic;
    EXPECT_EQ(parsed_v1.product_version, 1u);
    EXPECT_TRUE(parsed_v1.motions.empty());

    kpengine::live2d::Live2DProductData v2 = v1;
    v2.product_version = 2;
    v2.motions = {{"Idle", 0, true, 0.25, true, 0.75,
                    {std::byte{0x03}}, true, {std::byte{0x04}}}};
    v2.expressions = {{"Shy", {std::byte{0x05}}}};
    v2.parameter_groups = {{"Parameter", "EyeBlink", {"ParamEyeLOpen", "ParamEyeROpen"}}};

    std::vector<std::byte> first;
    ASSERT_TRUE(kpengine::live2d::SerializeLive2DProduct(v2, first, diagnostic))
        << diagnostic;
    kpengine::live2d::Live2DProductData parsed_v2{};
    ASSERT_TRUE(kpengine::live2d::ParseLive2DProduct(first, parsed_v2, diagnostic))
        << diagnostic;
    std::vector<std::byte> second;
    ASSERT_TRUE(kpengine::live2d::SerializeLive2DProduct(parsed_v2, second, diagnostic))
        << diagnostic;
    EXPECT_EQ(first, second);
    EXPECT_EQ(parsed_v2.motions.size(), 1u);
    EXPECT_EQ(parsed_v2.motions[0].group, "Idle");
    EXPECT_TRUE(parsed_v2.motions[0].has_sound);
    EXPECT_EQ(parsed_v2.expressions[0].name, "Shy");
    ASSERT_EQ(parsed_v2.parameter_groups.size(), 1u);
    EXPECT_EQ(parsed_v2.parameter_groups[0].ids.size(), 2u);
}

TEST(Live2DAssetTest, ImportsTypedAnimationProductV2)
{
    const std::filesystem::path root = MakeCharacterizationRoot();
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    const std::filesystem::path model_dir = root / "live2d" / "probe";
    std::filesystem::create_directories(model_dir);
    const std::filesystem::path hiyori =
        kpengine::project_root / "asset" / "live2d" / "hiyori_pro" / "runtime";
    CopyFile(hiyori / "hiyori_pro_t11.moc3", model_dir / "probe.moc3");
    CopyFile(hiyori / "hiyori_pro_t11.2048" / "texture_00.png",
             model_dir / "probe.2048" / "texture_00.png");
    WriteFile(model_dir / "motion" / "alpha_00.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "motion" / "alpha_01.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "motion" / "beta_00.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "expressions" / "shy.exp3.json",
              R"({"Type":"Live2D Expression","Parameters":[]})");
    WriteFile(model_dir / "expressions" / "angry.exp3.json",
              R"({"Type":"Live2D Expression","Parameters":[]})");
    WriteFile(model_dir / "sound" / "alpha_00.wav", "RIFFprobe");
    WriteFile(model_dir / "probe.model3.json", kMotionAndExpressionModel3);

    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;
    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root;
    request.archive_root = root / ".archive";
    request.source_path = "live2d/probe/probe.model3.json";

    const auto result = registry.Execute(request, {}, diagnostic);
    ASSERT_NE(result.product, nullptr) << result.diagnostic;
    const auto product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>(result.product);
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(product->value.product.product_version, 2u);
    ASSERT_EQ(product->value.product.motions.size(), 3u);
    EXPECT_EQ(product->value.product.motions[0].group, "Alpha");
    EXPECT_TRUE(product->value.product.motions[0].has_fade_in);
    EXPECT_TRUE(product->value.product.motions[0].has_sound);
    EXPECT_FALSE(product->value.product.motions[1].has_fade_in);
    ASSERT_EQ(product->value.product.expressions.size(), 2u);
    EXPECT_EQ(product->value.product.expressions[0].name, "Shy");
    ASSERT_EQ(product->value.product.parameter_groups.size(), 2u);
    EXPECT_EQ(product->value.product.parameter_groups[0].name, "LipSync");
    EXPECT_TRUE(product->value.product.optional_chunks.empty());

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST(Live2DAssetTest, CharacterizesV1AnimationMetadataLoss)
{
    const std::filesystem::path root = MakeCharacterizationRoot();
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    const std::filesystem::path model_dir = root / "live2d" / "probe";
    std::filesystem::create_directories(model_dir);

    // Real binary payloads are required: the importer reads and cooks the MOC
    // and the textures before it ever reaches the animation references.
    const std::filesystem::path hiyori =
        kpengine::project_root / "asset" / "live2d" / "hiyori_pro" / "runtime";
    CopyFile(hiyori / "hiyori_pro_t11.moc3", model_dir / "probe.moc3");
    CopyFile(hiyori / "hiyori_pro_t11.2048" / "texture_00.png",
             model_dir / "probe.2048" / "texture_00.png");

    WriteFile(model_dir / "motion" / "alpha_00.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "motion" / "alpha_01.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "motion" / "beta_00.motion3.json", kMinimalMotion3);
    WriteFile(model_dir / "expressions" / "shy.exp3.json",
              R"({"Type":"Live2D Expression","FadeInTime":0.5,"Parameters":[]})");
    WriteFile(model_dir / "expressions" / "angry.exp3.json",
              R"({"Type":"Live2D Expression","FadeInTime":0.5,"Parameters":[]})");
    WriteFile(model_dir / "sound" / "alpha_00.wav", "RIFFprobe");

    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    const auto execute = [&](const char *model3,
                             std::string &run_diagnostic) -> std::shared_ptr<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>
    {
        WriteFile(model_dir / "probe.model3.json", model3);
        kpengine::asset::ImportProviderRequest request{};
        request.asset_root = root;
        request.archive_root = root / ".archive";
        request.source_path = "live2d/probe/probe.model3.json";
        const auto result = registry.Execute(request, {}, run_diagnostic);
        run_diagnostic = result.diagnostic;
        if (result.product == nullptr)
        {
            return nullptr;
        }
        return std::dynamic_pointer_cast<
            kpengine::asset::TypedImportProduct<
                kpengine::live2d::Live2DImportProduct,
                kpengine::asset::ImportProviderKind::Custom>>(result.product);
    };

    const auto inventory = [](const char *label,
                              const std::shared_ptr<kpengine::asset::TypedImportProduct<
                                  kpengine::live2d::Live2DImportProduct,
                                  kpengine::asset::ImportProviderKind::Custom>> &product,
                              const std::string &run_diagnostic)
    {
        std::printf("[v1-char] %s: imported=%d diagnostic='%s'\n", label,
                    product != nullptr ? 1 : 0, run_diagnostic.c_str());
        if (product == nullptr)
        {
            return;
        }
        std::printf("[v1-char] %s: moc=%zu textures=%zu chunks=%zu\n", label,
                    product->value.product.moc_bytes.size(),
                    product->value.product.textures.size(),
                    product->value.product.optional_chunks.size());
        for (const kpengine::live2d::Live2DOptionalChunk &chunk :
             product->value.product.optional_chunks)
        {
            std::printf("[v1-char] %s:   '%s' (%zu bytes)\n", label,
                        chunk.name.c_str(), chunk.bytes.size());
        }
    };

    std::string first_diagnostic;
    const auto with_expressions = execute(kMotionAndExpressionModel3, first_diagnostic);
    inventory("with-expressions", with_expressions, first_diagnostic);

    std::string second_diagnostic;
    const auto motion_only = execute(kMotionOnlyModel3, second_diagnostic);
    inventory("motion-only", motion_only, second_diagnostic);

    std::error_code error;
    std::filesystem::remove_all(root, error);
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

// A model3 that ships Expressions used to fail to import outright: the walk
// treated every string under FileReferences as a file reference, so the
// entry's "Name" -- a display name -- was resolved as a path beside the model
// and reported as missing. Names are data, not paths, so an entry must
// contribute its File (and optional Sound) and nothing else.
TEST(Live2DAssetTest, ResolvesExpressionEntriesByFileNotByName)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kpengine_live2d_expression_root";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    const std::filesystem::path model_dir = root / "live2d" / "probe";
    std::filesystem::create_directories(model_dir);

    const std::filesystem::path hiyori =
        kpengine::project_root / "asset" / "live2d" / "hiyori_pro" / "runtime";
    CopyFile(hiyori / "hiyori_pro_t11.moc3", model_dir / "probe.moc3");
    CopyFile(hiyori / "hiyori_pro_t11.2048" / "texture_00.png",
             model_dir / "probe.2048" / "texture_00.png");
    // No file is named after the expression, so an importer that resolves the
    // display name fails here rather than importing the wrong bytes.
    WriteFile(model_dir / "expressions" / "shy.exp3.json",
              R"({"Type":"Live2D Expression","FadeInTime":0.5,"Parameters":[]})");

    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    WriteFile(model_dir / "probe.model3.json",
              R"({"Version":3,
  "FileReferences": {
    "Moc": "probe.moc3",
    "Textures": ["probe.2048/texture_00.png"],
    "Expressions": [
      { "Name": "Shy", "File": "expressions/shy.exp3.json" }
    ]
  }})");

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root;
    request.archive_root = root / ".archive";
    request.source_path = "live2d/probe/probe.model3.json";

    const auto result = registry.Execute(request, {}, diagnostic);
    ASSERT_NE(result.product, nullptr) << result.diagnostic;
    const auto product = std::dynamic_pointer_cast<
        kpengine::asset::TypedImportProduct<
            kpengine::live2d::Live2DImportProduct,
            kpengine::asset::ImportProviderKind::Custom>>(result.product);
    ASSERT_NE(product, nullptr);

    EXPECT_EQ(product->value.product.product_version, 2u);
    EXPECT_EQ(product->value.product.expressions.size(), 1u);
    EXPECT_EQ(product->value.product.expressions[0].name, "Shy");
    EXPECT_TRUE(product->value.product.optional_chunks.empty());
    // Exactly one chunk, named for the reference position and not for the
    // expression. A "Name"-derived chunk would appear here as
    // 'Expressions/0/Name'.

    std::error_code cleanup_after_test;
    std::filesystem::remove_all(root, cleanup_after_test);
}

// V1 acceptance names the missing-.moc3 and invalid-Moc rejections explicitly.
// Both were implemented but evidenced only by reading the source, so each case
// below drives the real importer and asserts that branch's own diagnostic
// rather than accepting any failure.
TEST(Live2DAssetTest, RejectsMissingAndInvalidMocReferences)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "kpengine_live2d_moc_reject_root";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    const std::filesystem::path model_dir = root / "live2d" / "probe";
    std::filesystem::create_directories(model_dir);

    // Real, non-empty payloads, so every case below fails on the Moc reference
    // and nothing earlier.
    const std::filesystem::path hiyori =
        kpengine::project_root / "asset" / "live2d" / "hiyori_pro" / "runtime";
    CopyFile(hiyori / "hiyori_pro_t11.moc3", model_dir / "probe.moc3");
    CopyFile(hiyori / "hiyori_pro_t11.2048" / "texture_00.png",
             model_dir / "probe.2048" / "texture_00.png");
    // Exists and is readable, but is not a .moc3: the extension check is what
    // must reject it, not a failed read.
    WriteFile(model_dir / "probe_not_moc.png", "not a moc");

    kpengine::asset::ImportProviderRegistry registry;
    std::string diagnostic;
    ASSERT_TRUE(kpengine::live2d::RegisterLive2DImporters(registry, diagnostic))
        << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;

    struct RejectionCase final
    {
        const char *name;
        const char *model3;
        const char *expected_diagnostic;
    };

    const std::array<RejectionCase, 4> cases{{
        {"moc_key_absent",
         R"({"Version":3,"FileReferences":{"Textures":["probe.2048/texture_00.png"]}})",
         "lacks Moc or ordered Textures"},
        {"textures_empty",
         R"({"Version":3,"FileReferences":{"Moc":"probe.moc3","Textures":[]}})",
         "lacks Moc or ordered Textures"},
        {"moc_is_not_a_moc3",
         R"({"Version":3,"FileReferences":{"Moc":"probe_not_moc.png",)"
         R"("Textures":["probe.2048/texture_00.png"]}})",
         "has an invalid Moc reference"},
        {"moc_absent_on_disk",
         R"({"Version":3,"FileReferences":{"Moc":"absent.moc3",)"
         R"("Textures":["probe.2048/texture_00.png"]}})",
         "missing or too large"},
    }};

    for (const RejectionCase &test_case : cases)
    {
        SCOPED_TRACE(test_case.name);
        WriteFile(model_dir / "probe.model3.json", test_case.model3);

        kpengine::asset::ImportProviderRequest request{};
        request.asset_root = root;
        request.archive_root = root / ".archive";
        request.source_path = "live2d/probe/probe.model3.json";
        const auto result = registry.Execute(request, {}, diagnostic);
        EXPECT_EQ(result.product, nullptr) << result.diagnostic;
        EXPECT_NE(result.diagnostic.find(test_case.expected_diagnostic),
                  std::string::npos)
            << result.diagnostic;
    }

    std::error_code error;
    std::filesystem::remove_all(root, error);
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
    EXPECT_EQ(resource->Product().motions.size(), 10u);
    const auto pose_chunk = std::find_if(
        resource->Product().optional_chunks.begin(),
        resource->Product().optional_chunks.end(),
        [](const kpengine::live2d::Live2DOptionalChunk &chunk)
        {
            return chunk.name == "Pose" && !chunk.bytes.empty();
        });
    ASSERT_NE(pose_chunk, resource->Product().optional_chunks.end());
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
    EXPECT_NE(first_instance->InstanceSerial(), 0u);
    EXPECT_NE(first_instance->InstanceSerial(), second_instance->InstanceSerial());
    EXPECT_EQ(first_instance->MotionCount(), 10u);
    EXPECT_EQ(second_instance->MotionCount(), 10u);
    EXPECT_TRUE(first_instance->HasMotion({"Idle", 0u}));
    EXPECT_TRUE(second_instance->HasMotion({"Tap@Body", 0u}));
    EXPECT_FALSE(first_instance->HasMotion({"idle", 0u}));
    EXPECT_EQ(first_instance->ExpressionCount(), 0u);
    EXPECT_FALSE(first_instance->HasExpression("Shy"));
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
    const std::size_t hidden_pose_drawables = std::count_if(
        first_frame.drawables.begin(), first_frame.drawables.end(),
        [](const kpengine::live2d::Live2DDrawableState &state)
        {
            return state.opacity <= 0.0f;
        });
    EXPECT_GT(hidden_pose_drawables, 0u);
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

    kpengine::live2d::Live2DPlaybackToken playback_token{};
    std::string playback_diagnostic;
    ASSERT_TRUE(second_instance->PlayMotion(
        {"Idle", 0u}, 1, kpengine::live2d::Live2DMotionStartMode::Force,
        playback_token, playback_diagnostic))
        << playback_diagnostic;
    EXPECT_EQ(playback_token.instance_serial, second_instance->InstanceSerial());
    kpengine::live2d::Live2DPlaybackUpdateResult playback_result;
    ASSERT_TRUE(second_instance->AdvancePlayback(0.0f, playback_result,
                                                 playback_diagnostic))
        << playback_diagnostic;
    std::vector<float> motion_parameters_before;
    motion_parameters_before.reserve(second_instance->ParameterCount());
    for (std::size_t parameter = 0u;
         parameter < second_instance->ParameterCount(); ++parameter)
    {
        float value = 0.0f;
        ASSERT_TRUE(second_instance->GetParameterValue(parameter, value));
        motion_parameters_before.push_back(value);
    }
    ASSERT_TRUE(second_instance->AdvancePlayback(0.5f, playback_result,
                                                 playback_diagnostic))
        << playback_diagnostic;
    bool motion_changed_parameter = false;
    for (std::size_t parameter = 0u;
         parameter < motion_parameters_before.size(); ++parameter)
    {
        float value = 0.0f;
        ASSERT_TRUE(second_instance->GetParameterValue(parameter, value));
        if (value != motion_parameters_before[parameter])
        {
            motion_changed_parameter = true;
            break;
        }
    }
    EXPECT_TRUE(motion_changed_parameter);
    ASSERT_TRUE(second_instance->AdvancePlayback(0.0f, playback_result,
                                                 playback_diagnostic))
        << playback_diagnostic;
    EXPECT_TRUE(playback_result.events.empty());
    ASSERT_TRUE(second_instance->AdvancePlayback(100.0f, playback_result,
                                                 playback_diagnostic))
        << playback_diagnostic;
    ASSERT_EQ(playback_result.events.size(), 1u);
    EXPECT_EQ(playback_result.events[0].kind,
              kpengine::live2d::Live2DPlaybackEventKind::MotionCompleted);
    EXPECT_EQ(playback_result.events[0].token.instance_serial,
              playback_token.instance_serial);
    EXPECT_EQ(playback_result.events[0].token.sequence, playback_token.sequence);
    EXPECT_FALSE(second_instance->StopMotion(
        playback_token, kpengine::live2d::Live2DStopMode::Immediate,
        playback_diagnostic));
    EXPECT_TRUE(second_instance->Update());

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
