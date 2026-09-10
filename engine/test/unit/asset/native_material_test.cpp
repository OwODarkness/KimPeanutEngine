#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "asset/native_material.h"
#include "asset/material_loader.h"
#include "config/path.h"

namespace
{
    kpengine::asset::ImportedModelDocument MakeDocument()
    {
        kpengine::asset::ImportedModelDocument document;
        document.images.push_back({"*0", kpengine::asset::ImportedImageStorage::EmbeddedBytes,
                                   {}, "png", {std::byte{255}, std::byte{128}, std::byte{64}, std::byte{255}},
                                   1, 1, true});

        kpengine::asset::ImportedMaterialSource material;
        material.name = "shared";
        material.base_color = kpengine::Vector4f{0.8f, 0.7f, 0.6f, 1.0f};
        material.metallic = 0.25f;
        material.roughness = 0.75f;
        material.base_color_texture = "*0";
        material.metallic_roughness_texture = "*0";
        material.alpha_mode = kpengine::asset::ImportedAlphaMode::Mask;
        material.alpha_cutoff = 0.4f;
        document.materials.push_back(material);
        material.name = "equivalent";
        document.materials.push_back(material);
        return document;
    }

    kpengine::asset::NativeMaterialConversionSettings MakeSettings()
    {
        return {std::filesystem::temp_directory_path() / "kpengine_mi1_5", "shader/pbr_gbuffer.shader"};
    }
}

TEST(NativeMaterialTest, ProducesDeterministicProductsAndReusesEmbeddedImages)
{
    const auto document = MakeDocument();
    const auto settings = MakeSettings();
    const auto first = kpengine::asset::ConvertImportedMaterials(document, settings);
    const auto second = kpengine::asset::ConvertImportedMaterials(document, settings);

    ASSERT_EQ(first.materials.size(), 2u);
    // The same source is prepared once per semantic and reused by repeated
    // bindings and equivalent materials.
    ASSERT_EQ(first.embedded_images.size(), 2u);
    EXPECT_EQ(first.metrics.requested_texture_bindings, 6u);
    EXPECT_EQ(first.metrics.unique_cook_keys, 2u);
    EXPECT_EQ(first.metrics.texture_decode_count, 2u);
    EXPECT_EQ(first.metrics.texture_prepare_count, 2u);
    EXPECT_EQ(first.metrics.texture_cook_count, 2u);
    EXPECT_EQ(first.metrics.unique_texture_product_count, 2u);
    EXPECT_EQ(first.materials[0].bytes, first.materials[1].bytes);
    EXPECT_EQ(first.materials[0].bytes, second.materials[0].bytes);
    EXPECT_EQ(first.materials[0].content_hash, kpengine::asset::Sha256(first.materials[0].bytes));
    EXPECT_EQ(first.embedded_images[0].content_hash,
              kpengine::asset::Sha256(first.embedded_images[0].bytes));

    const auto &parameters = first.materials[0].material.parameters;
    const auto find = [&parameters](const std::string &name) -> const auto *
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == name) return &parameter;
        }
        return static_cast<const kpengine::asset::MaterialParameterSource *>(nullptr);
    };
    const auto *metallic = find("metallic_texture");
    const auto *roughness = find("roughness_texture");
    ASSERT_NE(metallic, nullptr);
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(metallic->texture_channel, kpengine::asset::MaterialTextureChannel::Blue);
    EXPECT_EQ(roughness->texture_channel, kpengine::asset::MaterialTextureChannel::Green);
    EXPECT_EQ(first.materials[0].material.surface.alpha_mode,
              kpengine::asset::MaterialAlphaMode::Mask);
}

TEST(NativeMaterialTest, EmitsPortableAndBlockCompressedTextureVariants)
{
    auto settings = MakeSettings();
    settings.emit_texture_profile_variants = true;
    const auto converted = kpengine::asset::ConvertImportedMaterials(MakeDocument(), settings);

    // The one embedded image is consumed once as Color and once as
    // PackedLinear; each semantic has a portable and a BC product.
    ASSERT_EQ(converted.embedded_images.size(), 4u);
    EXPECT_EQ(converted.metrics.requested_texture_bindings, 6u);
    EXPECT_EQ(converted.metrics.unique_cook_keys, 2u);
    EXPECT_EQ(converted.metrics.texture_decode_count, 2u);
    EXPECT_EQ(converted.metrics.texture_prepare_count, 2u);
    EXPECT_EQ(converted.metrics.texture_cook_count, 4u);
    EXPECT_EQ(converted.metrics.portable_encode_count, 2u);
    EXPECT_EQ(converted.metrics.block_encode_count, 2u);
    EXPECT_EQ(converted.metrics.bc_encoding.bc3.block_count, 2u);
    EXPECT_EQ(converted.metrics.bc_encoding.bc4.block_count, 0u);
    EXPECT_EQ(converted.metrics.bc_encoding.bc5.block_count, 0u);
    EXPECT_GT(converted.metrics.bc_encoding.bc3.encode_seconds, 0.0);
    EXPECT_EQ(converted.metrics.unique_texture_product_count, 4u);
    for (const auto &material : converted.materials)
    {
        kpengine::asset::ValidateNativeMaterialProduct(material.bytes);
        for (const auto &parameter : material.material.parameters)
        {
            if (parameter.type == kpengine::asset::MaterialParameterSourceType::Texture)
            {
                EXPECT_FALSE(parameter.block_compressed_path.empty());
            }
        }
    }

    const auto &base_color = converted.materials[0].material.parameters.front();
    EXPECT_EQ(base_color.name, "base_color");
    const auto base_color_texture = std::find_if(
        converted.materials[0].material.parameters.begin(),
        converted.materials[0].material.parameters.end(),
        [](const auto &parameter) { return parameter.name == "base_color_texture"; });
    ASSERT_NE(base_color_texture, converted.materials[0].material.parameters.end());
    EXPECT_NE(base_color_texture->block_compressed_path,
              std::get<std::string>(base_color_texture->value));
}

TEST(NativeMaterialTest, RejectsMalformedEmbeddedImages)
{
    kpengine::asset::ImportedModelDocument document;
    document.images.push_back({"bad", kpengine::asset::ImportedImageStorage::EmbeddedBytes,
                               {}, "png", {std::byte{1}, std::byte{2}, std::byte{3}}});
    kpengine::asset::ImportedMaterialSource material;
    material.base_color_texture = "bad";
    document.materials.push_back(material);

    try
    {
        (void)kpengine::asset::ConvertImportedMaterials(document, MakeSettings());
        FAIL() << "expected malformed image conversion to fail";
    }
    catch (const kpengine::asset::NativeMaterialConversionError &error)
    {
        EXPECT_EQ(error.Code(), kpengine::asset::NativeMaterialErrorCode::MalformedImage);
    }
}

TEST(NativeMaterialTest, RejectsEmissiveTextureUntilGBufferSupportExists)
{
    kpengine::asset::ImportedModelDocument document;
    document.materials.push_back([]
    {
        kpengine::asset::ImportedMaterialSource material;
        material.emissive_texture = "missing";
        return material;
    }());

    try
    {
        (void)kpengine::asset::ConvertImportedMaterials(document, MakeSettings());
        FAIL() << "expected unsupported emissive texture to fail";
    }
    catch (const kpengine::asset::NativeMaterialConversionError &error)
    {
        EXPECT_EQ(error.Code(), kpengine::asset::NativeMaterialErrorCode::UnsupportedSemantics);
    }
}

TEST(NativeMaterialTest, CanonicalBytesRoundTripThroughMaterialLoader)
{
    const auto settings = kpengine::asset::NativeMaterialConversionSettings{
        kpengine::GetAssetDirectory(), "shader/pbr_gbuffer.shader"};
    const auto converted = kpengine::asset::ConvertImportedMaterials(MakeDocument(), settings);
    const std::filesystem::path path =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive" / "materials" /
        (converted.materials[0].content_hash.ToHex() + ".material");
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        for (const std::byte byte : converted.materials[0].bytes)
        {
            const char value = static_cast<char>(byte);
            file.write(&value, 1);
        }
    }

    kpengine::asset::AssetRegisterInfo info{};
    ASSERT_TRUE(kpengine::asset::MaterialLoader{}.Load(path.string(), info));
    const auto material = std::dynamic_pointer_cast<kpengine::asset::MaterialResource>(info.resource);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->surface.alpha_mode, kpengine::asset::MaterialAlphaMode::Mask);
    const auto metallic = std::find_if(
        material->parameters.begin(), material->parameters.end(),
        [](const auto &parameter) { return parameter.name == "metallic_texture"; });
    ASSERT_NE(metallic, material->parameters.end());
    EXPECT_EQ(metallic->texture_channel, kpengine::asset::MaterialTextureChannel::Blue);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(NativeMaterialTest, RejectsMismatchedArchiveHashAndLayout)
{
    const auto settings = kpengine::asset::NativeMaterialConversionSettings{
        kpengine::GetAssetDirectory(), "shader/pbr_gbuffer.shader"};
    const auto converted = kpengine::asset::ConvertImportedMaterials(MakeDocument(), settings);
    const std::filesystem::path archive =
        std::filesystem::path(kpengine::GetAssetDirectory()) / ".archive";
    const std::vector<std::byte> &bytes = converted.materials[0].bytes;

    const std::filesystem::path mismatched_path =
        archive / "materials" /
        (kpengine::asset::Sha256("different-material").ToHex() + ".material");
    std::filesystem::create_directories(mismatched_path.parent_path());
    {
        std::ofstream file(mismatched_path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }

    kpengine::asset::AssetRegisterInfo info{};
    EXPECT_FALSE(kpengine::asset::MaterialLoader{}.Load(mismatched_path.string(), info));

    const std::filesystem::path wrong_layout_path =
        archive / "not-materials" /
        (converted.materials[0].content_hash.ToHex() + ".material");
    std::filesystem::create_directories(wrong_layout_path.parent_path());
    {
        std::ofstream file(wrong_layout_path, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    info = {};
    EXPECT_FALSE(kpengine::asset::MaterialLoader{}.Load(wrong_layout_path.string(), info));

    std::error_code error;
    std::filesystem::remove(mismatched_path, error);
    std::filesystem::remove(wrong_layout_path, error);
}
