#include <filesystem>
#include <fstream>
#include <array>
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
    BuildCatalog(kpengine::asset::AssetID material_id,
                 std::shared_ptr<kpengine::asset::MaterialResource> material_override = nullptr,
                 std::vector<kpengine::asset::AssetID> material_dependencies = {})
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
        auto material = material_override != nullptr
                            ? std::move(material_override)
                            : asset::AssetManager::GetInstance().GetResource<asset::MaterialResource>(material_id);
        std::vector<asset::AssetID> dependencies{program_id};
        dependencies.insert(dependencies.end(), material_dependencies.begin(), material_dependencies.end());
        build.records.push_back({material_id, std::move(material), std::move(dependencies)});
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

TEST(MaterialAssetResolverTest, PreservesPackedChannelsAndAlphaMaskInTemplate)
{
    using namespace kpengine;
    const asset::AssetID material_id{7100, 1, asset::AssetType::KPAT_Material};
    const asset::AssetID texture_id{7004, 1, asset::AssetType::KPAT_Texture};
    auto material = std::make_shared<asset::MaterialResource>();
    material->version = 2;
    material->shader_dependency_index = 0;
    material->surface.shading_model = asset::MaterialShadingModel::StandardPbr;
    material->surface.alpha_mode = asset::MaterialAlphaMode::Mask;
    material->surface.alpha_cutoff = 0.35f;
    material->surface.blend_mode = asset::MaterialBlendMode::Opaque;

    auto add_vector = [&material](const char *name, std::array<float, 4> value)
    {
        material->parameters.push_back({name, asset::MaterialParameterSourceType::Vector4, value});
    };
    auto add_scalar = [&material](const char *name, float value)
    {
        material->parameters.push_back({name, asset::MaterialParameterSourceType::Scalar, value});
    };
    auto add_texture = [&material](const char *name, asset::MaterialTextureChannel channel)
    {
        asset::MaterialParameterSource parameter{};
        parameter.name = name;
        parameter.type = asset::MaterialParameterSourceType::Texture;
        parameter.value = std::string{"packed.texture"};
        parameter.texture_color_space = asset::MaterialTextureColorSpace::Linear;
        parameter.texture_channel = channel;
        parameter.dependency_index = 1;
        material->parameters.push_back(std::move(parameter));
    };
    add_vector("base_color", {1.f, 1.f, 1.f, 1.f});
    add_scalar("metallic", 1.f);
    add_scalar("roughness", 1.f);
    add_scalar("occlusion", 1.f);
    add_scalar("normal_scale", 0.6f);
    add_texture("metallic_texture", asset::MaterialTextureChannel::Blue);
    add_texture("roughness_texture", asset::MaterialTextureChannel::Green);
    add_texture("occlusion_texture", asset::MaterialTextureChannel::Red);

    ReadyMaterialResolver resource_resolver{};
    render::MaterialSystem materials{};
    materials.SetResourceResolver(&resource_resolver);
    const auto catalog = BuildCatalog(material_id, material, {texture_id});
    ASSERT_NE(catalog, nullptr);
    render::MaterialAssetResolver resolver{materials, catalog};
    render::MaterialInstanceHandle instance;
    ASSERT_EQ(resolver.Resolve(material_id, instance).state,
              render::MaterialResourceState::Ready);

    const render::MaterialTemplateHandle template_handle = materials.GetInstanceTemplate(instance);
    const render::MaterialTemplateDesc *const desc = materials.FindTemplate(template_handle);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->pipeline_state.blend_mode, render::MaterialBlendMode::Opaque);
    ASSERT_GE(desc->parameters.size(), 13u);
    const auto &channels = desc->parameters[10].default_value;
    ASSERT_TRUE(std::holds_alternative<Vector4f>(channels));
    const Vector4f channel_values = std::get<Vector4f>(channels);
    EXPECT_FLOAT_EQ(channel_values[0], 2.0f);
    EXPECT_FLOAT_EQ(channel_values[1], 1.0f);
    EXPECT_FLOAT_EQ(channel_values[2], 0.0f);
    EXPECT_FLOAT_EQ(std::get<float>(desc->parameters[12].default_value), 0.35f);
    resolver.Clear();
}
