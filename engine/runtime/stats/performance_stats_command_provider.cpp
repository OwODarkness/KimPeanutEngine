#include "stats/performance_stats_command_provider.h"

#include <initializer_list>
#include <string>
#include <utility>

namespace kpengine::runtime
{
    namespace
    {
        constexpr const char *kSchema = "kimpeanut.profiler.v1";
        constexpr const char *kCommandNames[] = {"gpu-stats", "cpu-stats", "stats"};

        const char *GraphicsApiName(const GraphicsAPIType api)
        {
            switch (api)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL:
                return "opengl";
            case GraphicsAPIType::GRAPHICS_API_VULKAN:
                return "vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW:
            default:
                return "unknown";
            }
        }

        const char *RenderPassName(const render::RenderProfilePass pass)
        {
            switch (pass)
            {
            case render::RenderProfilePass::DirectionalShadow:
                return "directional_shadow";
            case render::RenderProfilePass::SpotShadow:
                return "spot_shadow";
            case render::RenderProfilePass::PointShadow:
                return "point_shadow";
            case render::RenderProfilePass::GBuffer:
                return "g_buffer";
            case render::RenderProfilePass::DeferredLighting:
                return "deferred_lighting";
            case render::RenderProfilePass::ToneMap:
                return "tone_map";
            case render::RenderProfilePass::CaptureView:
                return "capture_view";
            case render::RenderProfilePass::EditorComposite:
                return "editor_composite";
            case render::RenderProfilePass::Count:
            default:
                return "unknown";
            }
        }

        const char *CpuSubphaseName(const render::RenderProfileCpuSubphase subphase)
        {
            switch (subphase)
            {
            case render::RenderProfileCpuSubphase::SectionPacketBuild:
                return "section_packet_build";
            case render::RenderProfileCpuSubphase::ShadowStampFit:
                return "shadow_stamp_fit";
            case render::RenderProfileCpuSubphase::MaterialResolution:
                return "material_resolution";
            case render::RenderProfileCpuSubphase::UniformWrite:
                return "uniform_write";
            case render::RenderProfileCpuSubphase::DescriptorSearch:
                return "descriptor_search";
            case render::RenderProfileCpuSubphase::DescriptorAllocation:
                return "descriptor_allocation";
            case render::RenderProfileCpuSubphase::DescriptorUpdate:
                return "descriptor_update";
            case render::RenderProfileCpuSubphase::PipelineValidation:
                return "pipeline_validation";
            case render::RenderProfileCpuSubphase::Count:
            default:
                return "unknown";
            }
        }

        void AddOptional(command::CommandData &data, const std::string &name,
                         const std::optional<double> &value)
        {
            data[name] = value.has_value() ? command::CommandValue{*value}
                                            : command::CommandValue{std::monostate{}};
        }

        void AddOptional(command::CommandData &data, const std::string &name,
                         const std::optional<uint64_t> &value)
        {
            data[name] = value.has_value() ? command::CommandValue{*value}
                                            : command::CommandValue{std::monostate{}};
        }

        std::optional<double> SumPasses(
            const render::RenderProfileSnapshot &profile,
            const std::initializer_list<render::RenderProfilePass> passes)
        {
            double total = 0.0;
            bool has_sample = false;
            for (const render::RenderProfilePass pass : passes)
            {
                const auto &sample = profile.passes[static_cast<size_t>(pass)].gpu_time_ms;
                if (sample.has_value())
                {
                    total += *sample;
                    has_sample = true;
                }
            }
            return has_sample ? std::optional<double>{total} : std::nullopt;
        }

        void AddCommon(command::CommandData &data, const PerformanceStatsSnapshot &snapshot,
                       const bool json_requested)
        {
            const auto &profile = snapshot.profile;
            data["schema"] = std::string{kSchema};
            data["format"] = std::string{json_requested ? "json" : "text"};
            data["frame_number"] = profile.frame_number;
            AddOptional(data, "gpu_frame_number", profile.gpu_frame_number);
            data["graphics_api"] = std::string{GraphicsApiName(profile.graphics_api)};
            data["viewport_width"] = static_cast<uint64_t>(profile.viewport_width);
            data["viewport_height"] = static_cast<uint64_t>(profile.viewport_height);
            data["present_mode"] = profile.present_mode;
            data["triangles"] = snapshot.triangle_count;
            if (snapshot.gpu_usage_percent.has_value())
            {
                data["gpu_usage_percent"] = static_cast<double>(*snapshot.gpu_usage_percent);
            }
            else
            {
                data["gpu_usage_percent"] = std::monostate{};
            }
        }

        void AddGpuStats(command::CommandData &data, const PerformanceStatsSnapshot &snapshot)
        {
            const auto &profile = snapshot.profile;
            data["draw_calls"] = profile.draw_calls;
            data["sections"] = profile.sections;
            data["native_draw_calls"] = profile.native_draw_calls;
            data["shadow_cache_hits"] = profile.shadow_cache_hits;
            data["shadow_cache_misses"] = profile.shadow_cache_misses;
            data["descriptor_sets_created"] = profile.descriptor_sets_created;
            data["descriptor_pools_created"] = profile.descriptor_pools_created;

            for (size_t index = 0;
                 index < static_cast<size_t>(render::RenderProfilePass::Count); ++index)
            {
                const auto &pass = profile.passes[index];
                const std::string prefix =
                    std::string{"pass."} +
                    RenderPassName(static_cast<render::RenderProfilePass>(index));
                AddOptional(data, prefix + ".gpu_ms", pass.gpu_time_ms);
                data[prefix + ".draw_calls"] = pass.draw_calls;
                data[prefix + ".sections"] = pass.sections;
            }

            AddOptional(data, "g_buffer_ms",
                        profile.passes[static_cast<size_t>(render::RenderProfilePass::GBuffer)]
                            .gpu_time_ms);
            AddOptional(data, "shadow_ms",
                        SumPasses(profile, {render::RenderProfilePass::DirectionalShadow,
                                            render::RenderProfilePass::SpotShadow,
                                            render::RenderProfilePass::PointShadow}));
            AddOptional(data, "lighting_ms",
                        SumPasses(profile, {render::RenderProfilePass::DeferredLighting}));
            AddOptional(data, "post_process_ms",
                        SumPasses(profile, {render::RenderProfilePass::ToneMap,
                                            render::RenderProfilePass::CaptureView}));
            AddOptional(data, "imgui_ms",
                        SumPasses(profile, {render::RenderProfilePass::EditorComposite}));
            AddOptional(data, "total_gpu_ms",
                        SumPasses(profile, {render::RenderProfilePass::DirectionalShadow,
                                            render::RenderProfilePass::SpotShadow,
                                            render::RenderProfilePass::PointShadow,
                                            render::RenderProfilePass::GBuffer,
                                            render::RenderProfilePass::DeferredLighting,
                                            render::RenderProfilePass::ToneMap,
                                            render::RenderProfilePass::CaptureView,
                                            render::RenderProfilePass::EditorComposite}));
        }

        void AddCpuStats(command::CommandData &data, const PerformanceStatsSnapshot &snapshot)
        {
            const auto &profile = snapshot.profile;
            const auto &frame = snapshot.frame_loop;
            data["cpu_total_ms"] = profile.cpu_total_ms;
            data["cpu_scene_prepare_ms"] = profile.cpu_scene_prepare_ms;
            data["cpu_backend_begin_ms"] = profile.cpu_backend_begin_ms;
            data["cpu_record_ms"] = profile.cpu_record_ms;
            data["cpu_finalize_ms"] = profile.cpu_finalize_ms;
            data["cpu_present_ms"] = profile.cpu_present_ms;
            data["frame_total_ms"] = frame.frame_total_ms;
            data["game_wait_ms"] = frame.game_wait_ms;
            data["render_work_ms"] = frame.render_work_ms;
            data["frame_pacing_ms"] = frame.frame_pacing_ms;
            data["game_tick_work_ms"] = frame.game_tick_work_ms;
            data["game_tick_pacing_ms"] = frame.game_tick_pacing_ms;
            data["descriptor_search_cpu_ms"] = profile.descriptor_search_cpu_ms;
            data["descriptor_allocation_cpu_ms"] = profile.descriptor_allocation_cpu_ms;
            data["descriptor_update_cpu_ms"] = profile.descriptor_update_cpu_ms;
            data["pipeline_validation_cpu_ms"] = profile.pipeline_validation_cpu_ms;
            data["section_packet_build_cpu_ms"] = profile.cpu_section_packet_build_ms;
            data["shadow_stamp_fit_cpu_ms"] = profile.cpu_shadow_stamp_fit_ms;
            data["material_resolution_cpu_ms"] = profile.cpu_material_resolution_ms;
            data["uniform_write_cpu_ms"] = profile.cpu_uniform_write_ms;

            for (size_t index = 0;
                 index < static_cast<size_t>(render::RenderProfileCpuSubphase::Count); ++index)
            {
                const auto &subphase = profile.summary.cpu_subphases[index];
                const std::string prefix =
                    std::string{"subphase."} + CpuSubphaseName(
                        static_cast<render::RenderProfileCpuSubphase>(index));
                AddOptional(data, prefix + ".p50_ms", subphase.cpu_p50_ms);
                AddOptional(data, prefix + ".p95_ms", subphase.cpu_p95_ms);
            }
            data["section_packet_build_calls"] = profile.section_packet_build_calls;
            data["section_packets_built"] = profile.section_packets_built;
            data["shadow_stamp_evaluations"] = profile.shadow_stamp_evaluations;
            data["shadow_fit_evaluations"] = profile.shadow_fit_evaluations;
            data["material_resolution_calls"] = profile.material_resolution_calls;
            data["uniform_writes"] = profile.uniform_writes;
            data["uniform_write_bytes"] = profile.uniform_write_bytes;
            data["descriptor_searches"] = profile.descriptor_searches;
            data["descriptor_allocations"] = profile.descriptor_allocations;
            data["descriptor_updates"] = profile.descriptor_updates;
            data["pipeline_validation_calls"] = profile.pipeline_validation_calls;
            data["pipeline_bind_requests"] = profile.pipeline_bind_requests;
            data["pipeline_bind_emitted"] = profile.pipeline_bind_emitted;
            data["mesh_bind_requests"] = profile.mesh_bind_requests;
            data["mesh_bind_emitted"] = profile.mesh_bind_emitted;
            data["resource_binding_bind_requests"] = profile.resource_binding_bind_requests;
            data["resource_binding_bind_emitted"] = profile.resource_binding_bind_emitted;
        }

        command::CommandResult ExecuteStats(
            const command::CommandCall &call, const command::CommandContext &context,
            const PerformanceStatsSnapshotProvider &provider, const bool include_gpu,
            const bool include_cpu)
        {
            const auto argument = call.arguments.find("json");
            const bool json_requested = argument != call.arguments.end() &&
                                         std::get<bool>(argument->second);
            const PerformanceStatsSnapshot snapshot = provider();
            command::CommandData data;
            AddCommon(data, snapshot, json_requested);
            if (include_gpu)
            {
                AddGpuStats(data, snapshot);
            }
            if (include_cpu)
            {
                AddCpuStats(data, snapshot);
            }
            if (include_cpu && include_gpu)
            {
                const auto &summary = snapshot.profile.summary;
                data["summary_complete"] = summary.complete;
                data["summary_warmup_frames_completed"] =
                    static_cast<uint64_t>(summary.warmup_frames_completed);
                data["summary_samples_collected"] =
                    static_cast<uint64_t>(summary.samples_collected);
                data["summary_cpu_total_p50_ms"] = summary.cpu_total_p50_ms;
                data["summary_cpu_total_p95_ms"] = summary.cpu_total_p95_ms;
                data["summary_cpu_present_p50_ms"] = summary.cpu_present_p50_ms;
                data["summary_cpu_present_p95_ms"] = summary.cpu_present_p95_ms;
            }
            return {command::CommandStatus::Success,
                    json_requested ? "Performance stats JSON" : "Performance stats",
                    context.request_id, std::move(data)};
        }

        command::CommandDesc MakeDescriptor(
            const char *name, const char *help,
            const PerformanceStatsSnapshotProvider &provider, const bool include_gpu,
            const bool include_cpu)
        {
            return {name,
                    "RuntimePerformanceStats",
                    help,
                    command::CommandCategory::Render,
                    command::CommandFlags::AgentAllowed | command::CommandFlags::LuaAllowed,
                    {{{"json", command::CommandValueType::Boolean, false, false, {}}}},
                    [provider, include_gpu, include_cpu](const command::CommandCall &call,
                                                          const command::CommandContext &context)
                    {
                        return ExecuteStats(call, context, provider, include_gpu, include_cpu);
                    },
                    command::CommandThread::Game};
        }
    }

    bool PerformanceStatsCommandRegistrationResult::IsSuccess() const noexcept
    {
        return status == command::CommandRegistrationStatus::Registered &&
               registrations[0].IsValid() && registrations[1].IsValid() &&
               registrations[2].IsValid();
    }

    PerformanceStatsCommandRegistrationResult RegisterPerformanceStatsCommands(
        command::CommandRegistry &registry, PerformanceStatsSnapshotProvider provider)
    {
        PerformanceStatsCommandRegistrationResult result{};
        if (!provider)
        {
            result.diagnostic =
                "Performance stats command provider requires a snapshot provider";
            return result;
        }

        const std::array<command::CommandDesc, 3> descriptors{
            MakeDescriptor(kCommandNames[0], "Return GPU performance statistics", provider, true,
                           false),
            MakeDescriptor(kCommandNames[1], "Return CPU performance statistics", provider, false,
                           true),
            MakeDescriptor(kCommandNames[2], "Return combined CPU and GPU statistics", provider,
                           true, true)};
        for (size_t index = 0; index < descriptors.size(); ++index)
        {
            auto registration = registry.Register(descriptors[index]);
            if (!registration.IsSuccess())
            {
                result.status = registration.status;
                result.diagnostic = registration.diagnostic;
                return result;
            }
            result.registrations[index] = std::move(registration.registration);
        }
        result.status = command::CommandRegistrationStatus::Registered;
        return result;
    }
}
