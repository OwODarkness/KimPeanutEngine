#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "render/prepared_render_asset_catalog.h"

namespace
{
    using namespace kpengine;

    render::PreparedRenderAssetCatalogBuild MakeBuild(bool duplicate_record = false,
                                                       bool broken_dependency = false)
    {
        render::PreparedRenderAssetCatalogBuild build;
        build.graphics_api = GraphicsAPIType::GRAPHICS_API_VULKAN;
        const asset::AssetID vertex_id{1, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID fragment_id{2, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID program_id{3, 1, asset::AssetType::KPAT_ShaderProgram};
        const asset::AssetID white_id{4, 1, asset::AssetType::KPAT_Texture};
        const asset::AssetID normal_id{5, 1, asset::AssetType::KPAT_Texture};

        auto shader = [](ShaderStage stage)
        {
            auto value = std::make_shared<asset::ShaderResource>();
            value->status = asset::ShaderStatus::Ready;
            value->data = std::make_shared<data::ShaderData>();
            value->data->stage = stage;
            value->data->api = GraphicsAPIType::GRAPHICS_API_VULKAN;
            value->data->byte_code = {1};
            value->desc.stage = stage;
            value->format = ShaderFormat::SHADER_FORMAT_GLSL;
            return value;
        };
        build.records.push_back({vertex_id, shader(ShaderStage::SHADER_STAGE_VERTEX), {}});
        build.records.push_back({fragment_id, shader(ShaderStage::SHADER_STAGE_FRAGMENT), {}});
        auto program = std::make_shared<asset::ShaderProgramResource>();
        program->BindData(ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL,
                          vertex_id);
        program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL,
                          fragment_id);
        build.records.push_back({program_id, program,
                                 {broken_dependency
                                      ? asset::AssetID{99, 1, asset::AssetType::KPAT_Shader}
                                      : vertex_id,
                                  fragment_id}});
        auto texture = []
        {
            auto value = std::make_shared<asset::TextureResource>();
            value->data->width = 1;
            value->data->height = 1;
            value->data->pixels.resize(4, 0);
            return value;
        };
        build.records.push_back({white_id, texture(), {}});
        build.records.push_back({normal_id, texture(), {}});
        for (const auto &requirement : render::GetBuiltInRenderAssetRequirements())
        {
            build.built_ins[static_cast<size_t>(requirement.role)] =
                requirement.expected_type == asset::AssetType::KPAT_Texture
                    ? (requirement.role == render::BuiltInRenderAsset::DefaultWhiteTexture
                           ? white_id
                           : normal_id)
                    : program_id;
        }
        if (duplicate_record)
        {
            build.records.push_back(build.records.front());
        }
        return build;
    }
}

TEST(PreparedRenderAssetCatalogTest, ValidatesRolesDependenciesAndPinsPayloads)
{
    std::string diagnostic;
    auto catalog = render::PreparedRenderAssetCatalog::Create(MakeBuild(), diagnostic);
    ASSERT_TRUE(catalog.has_value()) << diagnostic;
    const asset::AssetID program_id{3, 1, asset::AssetType::KPAT_ShaderProgram};
    EXPECT_NE(catalog->Get<asset::ShaderProgramResource>(program_id), nullptr);
    EXPECT_EQ(catalog->ResolveDependency(program_id, 0, asset::AssetType::KPAT_Shader).id, 1u);
    EXPECT_EQ(catalog->GetBuiltIn(render::BuiltInRenderAsset::ToneMapProgram), program_id);
}

TEST(PreparedRenderAssetCatalogTest, RejectsDuplicateRecordsAndBrokenDependencies)
{
    std::string diagnostic;
    EXPECT_FALSE(render::PreparedRenderAssetCatalog::Create(
                     MakeBuild(true), diagnostic));
    EXPECT_FALSE(render::PreparedRenderAssetCatalog::Create(
                     MakeBuild(false, true), diagnostic));
}

TEST(PreparedRenderAssetCatalogTest, RejectsUnreadyOrMismatchedShaderPrograms)
{
    const auto expect_invalid = [](render::PreparedRenderAssetCatalogBuild build)
    {
        std::string diagnostic;
        EXPECT_FALSE(render::PreparedRenderAssetCatalog::Create(std::move(build), diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    };

    auto wrong_api = MakeBuild();
    wrong_api.graphics_api = GraphicsAPIType::GRAPHICS_API_OPENGL;
    expect_invalid(std::move(wrong_api));

    auto empty_artifact = MakeBuild();
    std::get<asset::ShaderPtr>(empty_artifact.records[0].payload)->data->byte_code.clear();
    expect_invalid(std::move(empty_artifact));

    auto missing_stage = MakeBuild();
    auto incomplete_program = std::make_shared<asset::ShaderProgramResource>();
    incomplete_program->BindData(ShaderStage::SHADER_STAGE_VERTEX,
                                 ShaderFormat::SHADER_FORMAT_GLSL,
                                 asset::AssetID{1, 1, asset::AssetType::KPAT_Shader});
    missing_stage.records[2].payload = incomplete_program;
    expect_invalid(std::move(missing_stage));

    auto mismatched_binding = MakeBuild();
    auto mismatched_program = std::make_shared<asset::ShaderProgramResource>();
    mismatched_program->BindData(ShaderStage::SHADER_STAGE_VERTEX,
                                 ShaderFormat::SHADER_FORMAT_GLSL,
                                 asset::AssetID{2, 1, asset::AssetType::KPAT_Shader});
    mismatched_program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT,
                                 ShaderFormat::SHADER_FORMAT_GLSL,
                                 asset::AssetID{2, 1, asset::AssetType::KPAT_Shader});
    mismatched_binding.records[2].payload = mismatched_program;
    expect_invalid(std::move(mismatched_binding));
}

TEST(PreparedRenderAssetCatalogTest, ConstCatalogLookupCannotRecoverMutablePayload)
{
    std::string diagnostic;
    const auto catalog = render::PreparedRenderAssetCatalog::Create(MakeBuild(), diagnostic);
    ASSERT_TRUE(catalog.has_value()) << diagnostic;
    const auto &const_catalog = *catalog;
    using LookupType = decltype(const_catalog.Get<asset::ShaderResource>(
        asset::AssetID{1, 1, asset::AssetType::KPAT_Shader}));
    static_assert(std::is_same_v<LookupType, std::shared_ptr<const asset::ShaderResource>>);
    EXPECT_NE(const_catalog.Get<asset::ShaderResource>(
                  asset::AssetID{1, 1, asset::AssetType::KPAT_Shader}),
              nullptr);
}

TEST(PreparedRenderAssetCatalogTest, BuiltInRequirementRolesMatchTheirOrdinals)
{
    const auto &requirements = render::GetBuiltInRenderAssetRequirements();
    for (size_t index = 0; index < requirements.size(); ++index)
    {
        EXPECT_EQ(requirements[index].role,
                  static_cast<render::BuiltInRenderAsset>(index));
    }
}
