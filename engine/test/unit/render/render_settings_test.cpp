#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "render/render_system.h"
#include "render/settings/render_settings.h"
#include "settings/settings_store.h"

using namespace kpengine;

TEST(RenderSettingsTest, RegistersRestartAndLiveSectionsWithTypedDefaults)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    const auto startup = registry.FindSection("render.startup");
    ASSERT_TRUE(startup.has_value());
    EXPECT_EQ(startup->apply_policy, settings::SettingApplyPolicy::RestartRequired);

    const auto path_tracing = registry.FindSection("render.path_tracing");
    ASSERT_TRUE(path_tracing.has_value());
    EXPECT_EQ(path_tracing->apply_policy, settings::SettingApplyPolicy::Live);

    const auto snapshot = registry.GetResolvedSnapshot();
    bool ray_tracing_support = false;
    ASSERT_TRUE(render::ResolveRenderStartupSettings(snapshot, ray_tracing_support));
    EXPECT_TRUE(ray_tracing_support);
    const render::PathTraceSettings path_settings =
        render::ResolvePathTraceSettings(snapshot);
    EXPECT_TRUE(path_settings.path_tracing_enabled);
    EXPECT_EQ(path_settings.samples_per_dispatch, 4u);
    EXPECT_EQ(path_settings.maximum_continuation_bounces, 8u);
}

TEST(RenderSettingsTest, ExplicitSessionOverridesDoNotOverwriteUnsetProperties)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    const auto overrides = render::MakeRenderSettingsSessionOverrides(
        {.path_tracing_enabled = false});
    ASSERT_TRUE(registry.SetSessionOverrides(overrides));
    const auto snapshot = registry.GetResolvedSnapshot();
    bool ray_tracing_support = false;
    ASSERT_TRUE(render::ResolveRenderStartupSettings(snapshot, ray_tracing_support));
    EXPECT_TRUE(ray_tracing_support);
    EXPECT_FALSE(render::ResolvePathTraceSettings(snapshot).path_tracing_enabled);
}

TEST(RenderSettingsTest, LoadsPersistentStartupValuesBeforeOwnerInitialization)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    const auto directory = std::filesystem::current_path() /
        ("kpengine-render-settings-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct RemoveDirectory
    {
        std::filesystem::path path;
        ~RemoveDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } remove_directory{directory};

    const auto path = directory / "settings.json";
    settings::SettingsStore project_store{path, settings::SettingSaveScope::Project};
    EXPECT_EQ(project_store.Load(registry).status, settings::SettingsStoreStatus::MissingFile);
    const settings::SettingsLayerOverrides persisted{
        {"render.startup", {{"ray_tracing_support", false}}},
        {"render.path_tracing", {{"path_tracing_enabled", false}}},
    };
    const auto save_result = project_store.Save(registry, persisted);
    ASSERT_TRUE(save_result) << save_result.diagnostic;

    settings::SettingsStore reopened{path, settings::SettingSaveScope::Project};
    ASSERT_TRUE(reopened.Load(registry));
    ASSERT_TRUE(registry.SetPersistentOverrides(
        settings::SettingSaveScope::Project, reopened.GetSnapshot().overrides));
    const auto startup = registry.GetResolvedSnapshot();

    bool ray_tracing_support = true;
    ASSERT_TRUE(render::ResolveRenderStartupSettings(startup, ray_tracing_support));
    EXPECT_FALSE(ray_tracing_support);
    EXPECT_FALSE(render::ResolvePathTraceSettings(startup).path_tracing_enabled);

    ASSERT_TRUE(registry.SetSessionOverrides(
        render::MakeRenderSettingsSessionOverrides(
            {.path_tracing_enabled = true})));
    const auto launched = registry.GetResolvedSnapshot();
    ASSERT_TRUE(render::ResolveRenderStartupSettings(launched, ray_tracing_support));
    EXPECT_FALSE(ray_tracing_support);
    EXPECT_TRUE(render::ResolvePathTraceSettings(launched).path_tracing_enabled);
}

TEST(RenderSettingsTest, RejectsLivePathTracingWhenRenderResourcesAreUnavailable)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    render::RenderSystem render_system;
    render::PathTraceSettings disabled_settings;
    disabled_settings.path_tracing_enabled = false;
    ASSERT_TRUE(render_system.RequestPathTraceSettings(disabled_settings));
    render::RenderSettingsProvider provider{render_system};
    auto binding = registry.BindProvider("render.path_tracing", provider);
    ASSERT_TRUE(binding);

    const auto snapshot = registry.GetResolvedSnapshot();
    ASSERT_EQ(snapshot.sections.size(), 5u);
    const auto section = std::find_if(
        snapshot.sections.begin(), snapshot.sections.end(),
        [](const settings::ResolvedSettingsSection &candidate)
        {
            return candidate.key == "render.path_tracing";
        });
    ASSERT_NE(section, snapshot.sections.end());
    const auto edit = registry.SubmitEdit(
        "render.path_tracing", section->revision,
        {{"hybrid_ray_query_shadows_enabled", false}},
        settings::SettingsEditOrigin::Editor);
    ASSERT_EQ(edit.status, settings::SettingsEditStatus::Pending);

    EXPECT_EQ(registry.PumpProviders(), 1u);
    const auto result = registry.GetEditResult(edit.request_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, settings::SettingsEditStatus::Rejected);
    EXPECT_NE(result->diagnostic.find("scene renderer is unavailable"), std::string::npos);
}

TEST(RenderSettingsTest, ResolvesTypedLiveSettingsForEachRenderSection)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    render::ScreenSpaceAoSettings ao;
    ao.enabled = false;
    ao.radius = 1.25f;
    ao.bias = 0.05f;
    ao.strength = 2.0f;
    ao.quality = render::ScreenSpaceAoQuality::High;
    render::TemporalAaSettings taa;
    taa.enabled = false;
    taa.history_weight = 0.75f;
    render::HeightFogSettings fog;
    fog.enabled = true;
    fog.affect_sky = true;
    fog.density = 0.03f;
    fog.color = {0.2f, 0.4f, 0.8f};

    settings::SettingsLayerOverrides overrides;
    overrides["render.screen_space_ao"] = render::MakeScreenSpaceAoSettingValues(ao);
    overrides["render.temporal_aa"] = render::MakeTemporalAaSettingValues(taa);
    overrides["render.height_fog"] = render::MakeHeightFogSettingValues(fog);
    ASSERT_TRUE(registry.SetSessionOverrides(std::move(overrides)));

    const auto snapshot = registry.GetResolvedSnapshot();
    const auto resolved_ao = render::ResolveScreenSpaceAoSettings(snapshot);
    EXPECT_EQ(resolved_ao.enabled, ao.enabled);
    EXPECT_FLOAT_EQ(resolved_ao.radius, ao.radius);
    EXPECT_EQ(resolved_ao.quality, ao.quality);
    const auto resolved_taa = render::ResolveTemporalAaSettings(snapshot);
    EXPECT_EQ(resolved_taa.enabled, taa.enabled);
    EXPECT_FLOAT_EQ(resolved_taa.history_weight, taa.history_weight);
    const auto resolved_fog = render::ResolveHeightFogSettings(snapshot);
    EXPECT_EQ(resolved_fog.enabled, fog.enabled);
    EXPECT_EQ(resolved_fog.affect_sky, fog.affect_sky);
    EXPECT_FLOAT_EQ(resolved_fog.density, fog.density);
    EXPECT_EQ(resolved_fog.color, fog.color);
}

TEST(RenderSettingsTest, RejectsLiveSettingsWithoutWaitingForASceneRenderer)
{
    settings::SettingsRegistry registry;
    ASSERT_TRUE(render::RegisterRenderSettings(registry));
    ASSERT_TRUE(registry.Seal());

    render::RenderSystem render_system;
    render::RenderSettingsProvider provider{render_system};
    auto ao_binding = registry.BindProvider("render.screen_space_ao", provider);
    auto taa_binding = registry.BindProvider("render.temporal_aa", provider);
    auto fog_binding = registry.BindProvider("render.height_fog", provider);
    ASSERT_TRUE(ao_binding);
    ASSERT_TRUE(taa_binding);
    ASSERT_TRUE(fog_binding);

    const auto submit = [&registry](const char *section_key, const char *property_key,
                                    reflection::ReflectionValue value)
    {
        const auto snapshot = registry.GetResolvedSnapshot();
        const auto section = std::find_if(
            snapshot.sections.begin(), snapshot.sections.end(),
            [section_key](const settings::ResolvedSettingsSection &candidate)
            {
                return candidate.key == section_key;
            });
        if (section == snapshot.sections.end())
        {
            return uint64_t{0};
        }
        const auto edit = registry.SubmitEdit(
            section_key, section->revision, {{property_key, std::move(value)}},
            settings::SettingsEditOrigin::Editor);
        return edit.status == settings::SettingsEditStatus::Pending
                   ? edit.request_id
                   : uint64_t{0};
    };

    const uint64_t ao_edit = submit("render.screen_space_ao", "enabled", false);
    const uint64_t taa_edit = submit("render.temporal_aa", "enabled", false);
    const uint64_t fog_edit = submit("render.height_fog", "enabled", true);
    ASSERT_NE(ao_edit, 0u);
    ASSERT_NE(taa_edit, 0u);
    ASSERT_NE(fog_edit, 0u);
    EXPECT_FALSE(render_system.HasSceneRenderer());
    EXPECT_EQ(registry.PumpProviders(), 3u);

    for (const uint64_t request_id : {ao_edit, taa_edit, fog_edit})
    {
        const auto result = registry.GetEditResult(request_id);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->status, settings::SettingsEditStatus::Rejected);
        EXPECT_NE(result->diagnostic.find("scene renderer is unavailable"),
                  std::string::npos);
    }
}
