#include <gtest/gtest.h>

#include "render/material/material_system.h"

namespace
{
    class TestMaterialResolver final : public kpengine::render::IMaterialResourceResolver
    {
    public:
        kpengine::render::MaterialResolution template_resolution{
            kpengine::render::MaterialResourceState::Ready, {}};
        kpengine::render::MaterialResolution instance_resolution{
            kpengine::render::MaterialResourceState::Ready, {}};
        int template_requests = 0;
        int instance_requests = 0;

        kpengine::render::MaterialResolution ResolveTemplate(kpengine::render::MaterialTemplateHandle,
            const kpengine::render::MaterialTemplateDesc &) override
        {
            ++template_requests;
            return template_resolution;
        }

        kpengine::render::MaterialResolution ResolveInstance(kpengine::render::MaterialInstanceHandle,
            const kpengine::render::MaterialTemplateDesc &,
            const std::vector<kpengine::render::MaterialParameterValue> &) override
        {
            ++instance_requests;
            return instance_resolution;
        }
        void ReleaseTemplate(kpengine::render::MaterialTemplateHandle) override {}
        void ReleaseInstance(kpengine::render::MaterialInstanceHandle) override {}
    };

    kpengine::render::MaterialTemplateDesc MakeTemplateDesc()
    {
        kpengine::render::MaterialTemplateDesc desc{};
        desc.shader_program = {7, 1, kpengine::asset::AssetType::KPAT_ShaderProgram};
        desc.parameters.push_back({"roughness", 0.5f});
        desc.parameters.push_back(
            {"base_color", kpengine::Vector4f{1.0f, 1.0f, 1.0f, 1.0f}});
        desc.parameters.push_back({"base_color_texture",
                                   kpengine::render::MaterialTextureSamplerValue{
                                       {11, 1, kpengine::asset::AssetType::KPAT_Texture}, {}}});
        return desc;
    }
}

TEST(MaterialSystemTest, RejectsForgedAndStaleTemplateHandles)
{
    kpengine::render::MaterialSystem materials{};
    const auto handle = materials.CreateTemplate(MakeTemplateDesc());
    const kpengine::render::MaterialTemplateHandle forged{handle.id,
                                                           static_cast<uint16_t>(handle.generation + 1)};

    EXPECT_TRUE(handle.IsValid());
    EXPECT_FALSE(materials.IsTemplateValid(forged));
    EXPECT_EQ(materials.FindTemplate(forged), nullptr);
    EXPECT_FALSE(materials.DestroyTemplate(forged));
    EXPECT_TRUE(materials.DestroyTemplate(handle));
    EXPECT_FALSE(materials.IsTemplateValid(handle));
}

TEST(MaterialSystemTest, CopiesImmutableTemplateDescriptorAndKeepsDuplicatesIndependent)
{
    kpengine::render::MaterialSystem materials{};
    auto desc = MakeTemplateDesc();
    const auto first = materials.CreateTemplate(desc);
    const auto second = materials.CreateTemplate(desc);
    desc.parameters.front().name = "changed_after_creation";
    desc.pipeline_state.cull_mode = kpengine::render::MaterialCullMode::Front;
    desc.pipeline_state.double_sided = true;
    desc.bindless_texture_table_compatible = true;

    const auto *stored = materials.FindTemplate(first);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->parameters.front().name, "roughness");
    EXPECT_EQ(stored->pipeline_state.cull_mode, kpengine::render::MaterialCullMode::Back);
    EXPECT_FALSE(stored->pipeline_state.double_sided);
    EXPECT_FALSE(stored->bindless_texture_table_compatible);
    EXPECT_FALSE(first == second);
    EXPECT_TRUE(materials.DestroyTemplate(first));
    EXPECT_TRUE(materials.IsTemplateValid(second));
}

TEST(MaterialSystemTest, RetainsBindlessTextureConventionMetadata)
{
    kpengine::render::MaterialSystem materials{};
    auto desc = MakeTemplateDesc();
    desc.bindless_texture_table_compatible = true;

    const auto handle = materials.CreateTemplate(desc);
    const auto *stored = materials.FindTemplate(handle);

    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(stored->bindless_texture_table_compatible);
}

TEST(MaterialSystemTest, RetainsTemplateUntilReferencingInstanceIsDestroyed)
{
    kpengine::render::MaterialSystem materials{};
    const auto template_handle = materials.CreateTemplate(MakeTemplateDesc());
    const kpengine::render::MaterialInstanceDesc instance_desc{template_handle, {}};
    const auto instance_handle = materials.CreateInstance(instance_desc);

    ASSERT_TRUE(instance_handle.IsValid());
    EXPECT_EQ(materials.GetInstanceTemplate(instance_handle), template_handle);
    EXPECT_FALSE(materials.DestroyTemplate(template_handle));
    EXPECT_TRUE(materials.DestroyInstance(instance_handle));
    EXPECT_FALSE(materials.IsInstanceValid(instance_handle));
    EXPECT_TRUE(materials.DestroyTemplate(template_handle));
}

TEST(MaterialSystemTest, UsesDefaultsAndAcceptsSparseTypedOverrides)
{
    kpengine::render::MaterialSystem materials{};
    const auto template_handle = materials.CreateTemplate(MakeTemplateDesc());
    const auto roughness_id = materials.FindParameterID(template_handle, "roughness");
    const auto color_id = materials.FindParameterID(template_handle, "base_color");
    const auto texture_id = materials.FindParameterID(template_handle, "base_color_texture");
    const kpengine::render::MaterialInstanceDesc instance_desc{
        template_handle, {{roughness_id, 0.8f}}};
    const auto instance_handle = materials.CreateInstance(instance_desc);

    ASSERT_TRUE(instance_handle.IsValid());
    ASSERT_NE(materials.GetParameterValue(instance_handle, roughness_id), nullptr);
    EXPECT_EQ(std::get<float>(*materials.GetParameterValue(instance_handle, roughness_id)), 0.8f);
    ASSERT_NE(materials.GetParameterValue(instance_handle, color_id), nullptr);
    EXPECT_EQ(std::get<kpengine::Vector4f>(*materials.GetParameterValue(instance_handle, color_id)),
              (kpengine::Vector4f{1.0f, 1.0f, 1.0f, 1.0f}));
    ASSERT_NE(materials.GetParameterValue(instance_handle, texture_id), nullptr);
    EXPECT_EQ(std::get<kpengine::render::MaterialTextureSamplerValue>(
                  *materials.GetParameterValue(instance_handle, texture_id)).texture_asset.type,
              kpengine::asset::AssetType::KPAT_Texture);

    EXPECT_TRUE(materials.UpdateInstance(instance_handle, {{color_id,
        kpengine::Vector4f{0.25f, 0.5f, 0.75f, 1.0f}}}));
    EXPECT_EQ(std::get<float>(*materials.GetParameterValue(instance_handle, roughness_id)), 0.5f);
    EXPECT_EQ(std::get<kpengine::Vector4f>(*materials.GetParameterValue(instance_handle, color_id)),
              (kpengine::Vector4f{0.25f, 0.5f, 0.75f, 1.0f}));
}

TEST(MaterialSystemTest, RejectsUnknownAndMismatchedParameterOverrides)
{
    kpengine::render::MaterialSystem materials{};
    const auto template_handle = materials.CreateTemplate(MakeTemplateDesc());
    const auto roughness_id = materials.FindParameterID(template_handle, "roughness");
    const auto texture_id = materials.FindParameterID(template_handle, "base_color_texture");
    const kpengine::render::MaterialParameterID unknown_id{99};

    EXPECT_FALSE(materials.FindParameterID(template_handle, "missing").IsValid());
    EXPECT_FALSE(materials.CreateInstance({template_handle, {{unknown_id, 1.0f}}}).IsValid());
    EXPECT_FALSE(materials.CreateInstance(
        {template_handle, {{roughness_id, kpengine::Vector4f{}}}}).IsValid());
    EXPECT_FALSE(materials.CreateInstance({template_handle, {{texture_id, 0.1f}}}).IsValid());
    EXPECT_FALSE(materials.CreateInstance(
        {template_handle, {{roughness_id, 0.1f}, {roughness_id, 0.2f}}}).IsValid());

    auto invalid_texture_desc = MakeTemplateDesc();
    invalid_texture_desc.parameters.back().default_value =
        kpengine::render::MaterialTextureSamplerValue{
            {12, 1, kpengine::asset::AssetType::KPAT_ShaderProgram}, {}};
    EXPECT_FALSE(materials.CreateTemplate(invalid_texture_desc).IsValid());
}

TEST(MaterialSystemTest, RejectsStaleInstanceUpdates)
{
    kpengine::render::MaterialSystem materials{};
    const auto template_handle = materials.CreateTemplate(MakeTemplateDesc());
    const auto instance_handle = materials.CreateInstance({template_handle, {}});
    const auto roughness_id = materials.FindParameterID(template_handle, "roughness");

    ASSERT_TRUE(materials.DestroyInstance(instance_handle));
    EXPECT_FALSE(materials.UpdateInstance(instance_handle, {{roughness_id, 0.2f}}));
    EXPECT_EQ(materials.GetParameterValue(instance_handle, roughness_id), nullptr);
}

TEST(MaterialSystemTest, ResolvesEachReadyMaterialOnlyOnceAndClassifiesBlendMode)
{
    TestMaterialResolver resolver{};
    kpengine::render::MaterialSystem materials{};
    materials.SetResourceResolver(&resolver);
    auto alpha_desc = MakeTemplateDesc();
    alpha_desc.pipeline_state.blend_mode = kpengine::render::MaterialBlendMode::AlphaBlend;
    const auto template_handle = materials.CreateTemplate(alpha_desc);
    const auto instance_handle = materials.CreateInstance({template_handle, {}});

    EXPECT_EQ(resolver.template_requests, 1);
    EXPECT_EQ(resolver.instance_requests, 1);
    materials.RefreshResources();
    EXPECT_EQ(resolver.template_requests, 1);
    EXPECT_EQ(resolver.instance_requests, 1);
    EXPECT_EQ(materials.GetTemplateResolution(template_handle).state,
              kpengine::render::MaterialResourceState::Ready);
    EXPECT_EQ(materials.GetInstanceResolution(instance_handle).state,
              kpengine::render::MaterialResourceState::Ready);
    EXPECT_EQ(materials.GetDrawClass(instance_handle),
              kpengine::render::MaterialDrawClass::AlphaBlend);
}

TEST(MaterialSystemTest, FailedResourceResolutionPreventsInstanceReadiness)
{
    TestMaterialResolver resolver{};
    resolver.instance_resolution = {kpengine::render::MaterialResourceState::Failed,
                                    "missing texture"};
    kpengine::render::MaterialSystem materials{};
    materials.SetResourceResolver(&resolver);
    const auto template_handle = materials.CreateTemplate(MakeTemplateDesc());
    const auto instance_handle = materials.CreateInstance({template_handle, {}});

    EXPECT_EQ(materials.GetInstanceResolution(instance_handle).state,
              kpengine::render::MaterialResourceState::Failed);
    EXPECT_EQ(materials.GetInstanceResolution(instance_handle).diagnostic, "missing texture");
}
