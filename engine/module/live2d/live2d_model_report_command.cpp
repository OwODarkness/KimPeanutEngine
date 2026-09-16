#include "live2d_model_report_command.h"

#include <utility>

namespace kpengine::live2d
{
    runtime::command::CommandRegistrationResult RegisterLive2DModelReportCommand(
        runtime::command::CommandRegistry &registry, Live2DModelReportResolver resolver)
    {
        if (!resolver)
        {
            return {{}, runtime::command::CommandRegistrationStatus::InvalidDescriptor,
                    "Live2D model report command requires a model resolver"};
        }

        runtime::command::CommandDesc descriptor{
            "live2d.model_report",
            "Live2DRuntime",
            "Report the loaded Live2D product's drawable and blend-mode counts",
            runtime::command::CommandCategory::Engine,
            runtime::command::CommandFlags::AgentAllowed |
                runtime::command::CommandFlags::LuaAllowed,
            {},
            [resolver = std::move(resolver)](
                const runtime::command::CommandCall &call,
                const runtime::command::CommandContext &context)
            {
                Live2DModelReport report;
                if (!resolver(report))
                {
                    return runtime::command::CommandResult{
                        runtime::command::CommandStatus::Failed,
                        "No Live2D product is loaded in the current mode",
                        context.request_id,
                        {}};
                }

                const Live2DRenderFeatureReport &features = report.features;
                return runtime::command::CommandResult{
                    runtime::command::CommandStatus::Success,
                    "Live2D model report",
                    context.request_id,
                    {{"model", report.model_path},
                     {"drawable_count",
                      static_cast<std::uint64_t>(features.drawable_count)},
                     {"normal_drawable_count",
                      static_cast<std::uint64_t>(features.normal_drawable_count)},
                     {"additive_drawable_count",
                      static_cast<std::uint64_t>(features.additive_drawable_count)},
                     {"multiplicative_drawable_count",
                      static_cast<std::uint64_t>(
                          features.multiplicative_drawable_count)},
                     {"unknown_blend_mode_count",
                      static_cast<std::uint64_t>(features.unknown_blend_mode_count)},
                     {"covers_all_blend_modes", features.CoversAllBlendModes()},
                     {"has_typed_playback", report.capabilities.has_typed_playback},
                     {"has_secondary_behavior", report.capabilities.has_secondary_behavior},
                     {"has_physics", report.capabilities.has_physics},
                     {"has_pose", report.capabilities.has_pose},
                     {"has_hit_areas", report.capabilities.has_hit_areas},
                     {"has_user_data", report.capabilities.has_user_data},
                     {"requires_reimport_for_secondary_behavior",
                      report.capabilities.requires_reimport_for_secondary_behavior},
                     {"behavior_mask", static_cast<std::uint64_t>(report.behavior_mask)},
                     {"update_sequence", report.update_sequence},
                     {"behavior_replay_available", report.behavior_replay_available},
                     {"behavior_replay_passed", report.behavior_replay_passed},
                     {"behavior_replay_deterministic",
                      report.behavior_replay_deterministic},
                     {"behavior_state", report.behavior_state},
                     {"behavior_transition_sequence",
                      report.behavior_transition_sequence},
                     {"behavior_history_count", report.behavior_history_count},
                     {"behavior_replay_diagnostic",
                      report.behavior_replay_diagnostic}}};
            },
            runtime::command::CommandThread::Game};

        return registry.Register(std::move(descriptor));
    }
}
