#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/material.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "config/path.h"
#include "render/material/material_asset_resolver.h"
#include "render/prepared_render_asset_catalog.h"

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
            std::filesystem::path(kpengine::GetAssetDirectory()) / ".test_material" /
            "resolver";
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

    std::shared_ptr<const kpengine::render::PreparedRenderAssetCatalog>
    BuildCatalog(kpengine::asset::AssetID material_id)
    {
        using namespace kpengine;
        render::PreparedRenderAssetCatalogBuild build;
        build.graphics_api = GraphicsAPIType::GRAPHICS_API_OPENGL;
        const asset::AssetID vertex_id{7001, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID fragment_id{7002, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID program_id{7003, 1, asset::AssetType::KPAT_ShaderProgram};
        auto make_shader = [](ShaderStage stage)
        {
            auto shader = std::make_shared<asset::ShaderResource>();
            shader->status = asset::ShaderStatus::Ready;
            shader->data = std::make_shared<data::ShaderData>();
            shader->data->stage = stage;
            shader->data->api = GraphicsAPIType::GRAPHICS_API_OPENGL;
            shader->data->source = "void main() {}";
            shader->desc.stage = stage;
            shader->format = ShaderFormat::SHADER_FORMAT_GLSL;
            return shader;
        };
        build.records.push_back({vertex_id, make_shader(ShaderStage::SHADER_STAGE_VERTEX), {}});
        build.records.push_back({fragment_id, make_shader(ShaderStage::SHADER_STAGE_FRAGMENT), {}});
        auto program = std::make_shared<asset::ShaderProgramResource>();
        program->BindData(ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL,
                          vertex_id);
        program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL,
                          fragment_id);
        build.records.push_back({program_id, program, {vertex_id, fragment_id}});
        auto make_texture = [](uint8_t value)
        {
            auto texture = std::make_shared<asset::TextureResource>();
            texture->data->width = 1;
            texture->data->height = 1;
            texture->data->pixels.resize(4, value);
            return texture;
        };
        const asset::AssetID white_id{7004, 1, asset::AssetType::KPAT_Texture};
        const asset::AssetID normal_id{7005, 1, asset::AssetType::KPAT_Texture};
        build.records.push_back({white_id, make_texture(255), {}});
        build.records.push_back({normal_id, make_texture(128), {}});
        build.records.push_back({material_id,
                                 asset::AssetManager::GetInstance().GetResource<asset::MaterialResource>(material_id),
                                 {program_id}});
        for (const auto &requirement : render::GetBuiltInRenderAssetRequirements())
        {
            build.built_ins[static_cast<size_t>(requirement.role)] =
                requirement.expected_type == asset::AssetType::KPAT_Texture
                    ? (requirement.role == render::BuiltInRenderAsset::DefaultWhiteTexture
                           ? white_id
                           : normal_id)
                    : program_id;
        }
        std::string diagnostic;
        auto catalog = render::PreparedRenderAssetCatalog::Create(std::move(build), diagnostic);
        return catalog ? std::make_shared<const render::PreparedRenderAssetCatalog>(std::move(*catalog))
                       : nullptr;
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
    const auto catalog = BuildCatalog(material_id);
    ASSERT_NE(catalog, nullptr);
    kpengine::render::MaterialAssetResolver resolver{materials, catalog};
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
    kpengine::render::MaterialAssetResolver resolver{materials, nullptr};
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
    EXPECT_FALSE(material_id.IsValid());
    const auto resolution = resolver.Resolve(material_id, instance);
    EXPECT_EQ(resolution.state, kpengine::render::MaterialResourceState::Failed);
    EXPECT_EQ(resolution.diagnostic, "static mesh source has an invalid material asset");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
