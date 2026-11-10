#ifndef KPENGINE_RUNTIME_RENDER_SETTINGS_RENDER_SETTINGS_H
#define KPENGINE_RUNTIME_RENDER_SETTINGS_RENDER_SETTINGS_H

#include <optional>

#include "render/height_fog_settings.h"
#include "render/path_trace_settings.h"
#include "render/screen_space_ao_settings.h"
#include "render/temporal_aa_settings.h"
#include "settings/settings_registry.h"

namespace kpengine::render
{
    class RenderSystem;

    settings::SettingsResult RegisterRenderSettings(
        settings::SettingsRegistry &registry);

    struct RenderSettingsSessionOptions
    {
        std::optional<bool> ray_tracing_support;
        std::optional<bool> path_tracing_enabled;
    };

    settings::SettingsLayerOverrides MakeRenderSettingsSessionOverrides(
        const RenderSettingsSessionOptions &options);

    bool ResolveRenderStartupSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        bool &ray_tracing_support) noexcept;

    PathTraceSettings ResolvePathTraceSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        PathTraceSettings base = {});

    ScreenSpaceAoSettings ResolveScreenSpaceAoSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        ScreenSpaceAoSettings base = {});
    TemporalAaSettings ResolveTemporalAaSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        TemporalAaSettings base = {});
    HeightFogSettings ResolveHeightFogSettings(
        const settings::SettingsResolvedSnapshot &snapshot,
        HeightFogSettings base = {});

    settings::SettingValues MakePathTraceSettingValues(
        const PathTraceSettings &settings);
    settings::SettingValues MakeScreenSpaceAoSettingValues(
        const ScreenSpaceAoSettings &settings);
    settings::SettingValues MakeTemporalAaSettingValues(
        const TemporalAaSettings &settings);
    settings::SettingValues MakeHeightFogSettingValues(
        const HeightFogSettings &settings);

    class RenderSettingsProvider final : public settings::ISettingsProvider
    {
    public:
        explicit RenderSettingsProvider(RenderSystem &render_system)
            : render_system_(render_system)
        {
        }

        void Apply(const settings::SettingsApplyRequest &request,
                   settings::SettingsCompletionHandle completion) override;

    private:
        RenderSystem &render_system_;
    };
}

#endif
