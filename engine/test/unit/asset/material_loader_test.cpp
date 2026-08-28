#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/material.h"

namespace
{
    std::filesystem::path MakeMaterialPath(const char *name)
    {
        return std::filesystem::temp_directory_path() / name;
    }

    void WriteMaterialFile(const std::filesystem::path &path, const std::string &contents)
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open()) << "failed to open " << path.string();
        file << contents;
    }
}

TEST(MaterialLoaderTest, LoadsVersionedUnlitMaterialSource)
{
    const std::filesystem::path path = MakeMaterialPath("kpengine_material_valid.material");
    WriteMaterialFile(path, R"({
        "version": 1,
        "shader": "../shader/simple_triangle.shader",
        "surface": {
            "shading_model": "unlit",
            "blend_mode": "opaque",
            "cull_mode": "back",
            "double_sided": false
        },
        "parameters": {
            "roughness": 0.5,
            "base_color": [1.0, 0.5, 0.25, 1.0],
            "base_color_texture": "../texture/wallpaper.jpg"
        }
    })");

    const kpengine::asset::AssetID id = kpengine::asset::AssetManager::GetInstance().LoadSync(path.string());
    const auto material = kpengine::asset::AssetManager::GetInstance().GetResource<kpengine::asset::MaterialResource>(id);

    ASSERT_TRUE(id.IsValid());
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(id.type, kpengine::asset::AssetType::KPAT_Material);
    EXPECT_EQ(material->shader_path, "../shader/simple_triangle.shader");
    EXPECT_EQ(material->surface.blend_mode, kpengine::asset::MaterialBlendMode::Opaque);
    ASSERT_EQ(material->parameters.size(), 3u);
    EXPECT_EQ(material->parameters[0].type, kpengine::asset::MaterialParameterSourceType::Vector4);
    EXPECT_EQ(material->parameters[1].type, kpengine::asset::MaterialParameterSourceType::Texture);
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(MaterialLoaderTest, RejectsUnknownFieldsAndInvalidParameterShapes)
{
    const std::filesystem::path unknown_path = MakeMaterialPath("kpengine_material_unknown.material");
    WriteMaterialFile(unknown_path, R"({
        "version": 1,
        "shader": "shader.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {},
        "typo": true
    })");
    EXPECT_FALSE(kpengine::asset::AssetManager::GetInstance().LoadSync(unknown_path.string()).IsValid());

    const std::filesystem::path invalid_path = MakeMaterialPath("kpengine_material_invalid_parameter.material");
    WriteMaterialFile(invalid_path, R"({
        "version": 1,
        "shader": "shader.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {"base_color": [1.0, 1.0, 1.0]}
    })");
    EXPECT_FALSE(kpengine::asset::AssetManager::GetInstance().LoadSync(invalid_path.string()).IsValid());

    std::error_code error;
    std::filesystem::remove(unknown_path, error);
    std::filesystem::remove(invalid_path, error);
}

TEST(MaterialLoaderTest, RejectsUnsupportedVersion)
{
    const std::filesystem::path path = MakeMaterialPath("kpengine_material_unsupported_version.material");
    WriteMaterialFile(path, R"({
        "version": 2,
        "shader": "shader.shader",
        "surface": {"shading_model": "unlit", "blend_mode": "opaque", "cull_mode": "back", "double_sided": false},
        "parameters": {}
    })");

    EXPECT_FALSE(kpengine::asset::AssetManager::GetInstance().LoadSync(path.string()).IsValid());
    std::error_code error;
    std::filesystem::remove(path, error);
}
