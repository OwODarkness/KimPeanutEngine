#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/material.h"
#include "render/material/material_asset_resolver.h"

namespace
{
    class ReadyMaterialResolver final : public kpengine::render::IMaterialResourceResolver
    {
    public:
        int template_requests = 0;
        int instance_requests = 0;

        kpengine::render::MaterialResolution ResolveTemplate(
            kpengine::render::MaterialTemplateHandle,
            const kpengine::render::MaterialTemplateDesc &) override
        {
            ++template_requests;
            return {kpengine::render::MaterialResourceState::Ready, {}};
        }

        kpengine::render::MaterialResolution ResolveInstance(
            kpengine::render::MaterialInstanceHandle,
            const kpengine::render::MaterialTemplateDesc &,
            const std::vector<kpengine::render::MaterialParameterValue> &) override
        {
            ++instance_requests;
            return {kpengine::render::MaterialResourceState::Ready, {}};
        }

        void ReleaseTemplate(kpengine::render::MaterialTemplateHandle) override {}
        void ReleaseInstance(kpengine::render::MaterialInstanceHandle) override {}
    };

    std::filesystem::path MakeResolverTestDirectory()
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / "kpengine_material_asset_resolver_test";
        std::filesystem::create_directories(directory);
        return directory;
    }

    void WriteFile(const std::filesystem::path &path, const std::string &contents)
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open()) << path.string();
        file << contents;
    }

    std::filesystem::path WriteValidMaterial(const std::filesystem::path &directory)
    {
        WriteFile(directory / "test.shader", R"({
            "version": 1,
            "shaders": [
                {"stage": "vertex", "format": "glsl", "file": "test.vert", "entry": "main"},
                {"stage": "fragment", "format": "glsl", "file": "test.frag", "entry": "main"}
            ]
        })");
        const std::filesystem::path material_path = directory / "test.material";
        WriteFile(material_path, R"({
            "version": 1,
            "shader": "test.shader",
            "surface": {
                "shading_model": "unlit",
                "blend_mode": "opaque",
                "cull_mode": "back",
                "double_sided": false
            },
            "parameters": {"base_color": [1.0, 1.0, 1.0, 1.0]}
        })");
        return material_path;
    }
}

TEST(MaterialAssetResolverTest, CachesOneTemplateAndDefaultInstancePerMaterialAsset)
{
    const std::filesystem::path directory = MakeResolverTestDirectory();
    const std::filesystem::path material_path = WriteValidMaterial(directory);
    auto &assets = kpengine::asset::AssetManager::GetInstance();
    const kpengine::asset::AssetID material_id = assets.LoadSync(material_path.string());

    ReadyMaterialResolver resource_resolver{};
    kpengine::render::MaterialSystem materials{};
    materials.SetResourceResolver(&resource_resolver);
    kpengine::render::MaterialAssetResolver resolver{materials};
    kpengine::render::MaterialInstanceHandle first;
    kpengine::render::MaterialInstanceHandle second;

    EXPECT_EQ(resolver.Resolve(material_id, first).state,
              kpengine::render::MaterialResourceState::Ready);
    EXPECT_EQ(resolver.Resolve(material_id, second).state,
              kpengine::render::MaterialResourceState::Ready);
    EXPECT_EQ(first, second);
    EXPECT_EQ(resolver.GetRecordCount(), 1u);
    EXPECT_EQ(resource_resolver.template_requests, 1);
    EXPECT_EQ(resource_resolver.instance_requests, 1);

    resolver.Clear();
    EXPECT_EQ(resolver.GetRecordCount(), 0u);
    EXPECT_FALSE(materials.IsInstanceValid(first));

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

TEST(MaterialAssetResolverTest, ReportsInvalidPendingAndBrokenReferences)
{
    ReadyMaterialResolver resource_resolver{};
    kpengine::render::MaterialSystem materials{};
    materials.SetResourceResolver(&resource_resolver);
    kpengine::render::MaterialAssetResolver resolver{materials};
    kpengine::render::MaterialInstanceHandle instance;

    EXPECT_EQ(resolver.Resolve({}, instance).state,
              kpengine::render::MaterialResourceState::Failed);
    EXPECT_EQ(resolver.Resolve({99, 1, kpengine::asset::AssetType::KPAT_Material}, instance).state,
              kpengine::render::MaterialResourceState::Pending);

    const std::filesystem::path directory = MakeResolverTestDirectory();
    const std::filesystem::path material_path = directory / "broken.material";
    WriteFile(material_path, R"({
        "version": 1,
        "shader": "missing.shader",
        "surface": {
            "shading_model": "unlit",
            "blend_mode": "opaque",
            "cull_mode": "back",
            "double_sided": false
        },
        "parameters": {}
    })");
    const kpengine::asset::AssetID material_id =
        kpengine::asset::AssetManager::GetInstance().LoadSync(material_path.string());
    const auto resolution = resolver.Resolve(material_id, instance);
    EXPECT_EQ(resolution.state, kpengine::render::MaterialResourceState::Failed);
    EXPECT_EQ(resolution.diagnostic, "material shader program could not be loaded");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
