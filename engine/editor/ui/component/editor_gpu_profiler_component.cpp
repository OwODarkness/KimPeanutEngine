#include "editor/ui/component/editor_gpu_profiler_component.h"

#include <cstdio>
#include <initializer_list>
#include <optional>
#include <sstream>

#include "imgui.h"
#include <nlohmann/json.hpp>
#include "editor/ui/editor_ui.h"
#include "runtime/engine.h"
#include "runtime/render/render_profile.h"
#include "runtime/render/render_system.h"

namespace kpengine::editor
{
    namespace
    {
        using render::RenderProfilePass;
        using render::RenderProfileSnapshot;
        using Json = nlohmann::json;

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

        const char *RenderPassName(const RenderProfilePass pass)
        {
            switch (pass)
            {
            case RenderProfilePass::DirectionalShadow:
                return "directional_shadow";
            case RenderProfilePass::SpotShadow:
                return "spot_shadow";
            case RenderProfilePass::PointShadow:
                return "point_shadow";
            case RenderProfilePass::GBuffer:
                return "g_buffer";
            case RenderProfilePass::DeferredLighting:
                return "deferred_lighting";
            case RenderProfilePass::RayTracingPathTrace:
                return "ray_tracing_path_trace";
            case RenderProfilePass::ToneMap:
                return "tone_map";
            case RenderProfilePass::RayTracingToneMap:
                return "ray_tracing_tone_map";
            case RenderProfilePass::CaptureView:
                return "capture_view";
            case RenderProfilePass::DebugView:
                return "debug_view";
            case RenderProfilePass::EditorComposite:
                return "editor_composite";
            case RenderProfilePass::RayTracingBlasBuild:
                return "ray_tracing_blas_build";
            case RenderProfilePass::RayTracingTlasBuild:
                return "ray_tracing_tlas_build";
            case RenderProfilePass::Count:
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
            case render::RenderProfileCpuSubphase::GraphExecute:
                return "graph_execute";
            case render::RenderProfileCpuSubphase::Count:
            default:
                return "unknown";
            }
        }

        Json OptionalMilliseconds(const std::optional<double> &milliseconds)
        {
            return milliseconds.has_value() ? Json(*milliseconds) : Json(nullptr);
        }

        std::optional<double> SumPasses(
            const RenderProfileSnapshot &profile,
            const std::initializer_list<RenderProfilePass> passes)
        {
            double total = 0.0;
            bool has_sample = false;
            for (const RenderProfilePass pass : passes)
            {
                const std::optional<double> &sample =
                    profile.passes[static_cast<size_t>(pass)].gpu_time_ms;
                if (sample.has_value())
                {
                    total += *sample;
                    has_sample = true;
                }
            }
            return has_sample ? std::optional<double>{total} : std::nullopt;
        }

        std::string FormatMilliseconds(const std::optional<double> &milliseconds)
        {
            if (!milliseconds.has_value())
            {
                return "N/A";
            }
            char value[32]{};
            std::snprintf(value, sizeof(value), "%.2f ms", *milliseconds);
            return value;
        }

        std::string FormatBytes(const uint64_t bytes)
        {
            constexpr double bytes_per_mib = 1024.0 * 1024.0;
            char value[48]{};
            std::snprintf(value, sizeof(value), "%.2f MiB",
                          static_cast<double>(bytes) / bytes_per_mib);
            return value;
        }

        std::string FormatPercent(const std::optional<float> &percent)
        {
            if (!percent.has_value())
            {
                return "N/A";
            }
            char value[32]{};
            std::snprintf(value, sizeof(value), "%.1f%%", *percent);
            return value;
        }

        void DrawInfoRow(const char *label, const std::string &value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", value.c_str());
        }

        const char *FeatureStatus(const bool available, const bool active)
        {
            if (!available)
            {
                return "unavailable";
            }
            return active ? "active" : "inactive";
        }

        double UnaccountedRecordTime(const RenderProfileSnapshot &profile)
        {
            const double measured = profile.cpu_render_world_snapshot_ms +
                                    profile.cpu_ray_tracing_scene_prepare_ms +
                                    profile.cpu_graph_execute_ms;
            return profile.cpu_record_ms > measured ? profile.cpu_record_ms - measured : 0.0;
        }

        void DrawStageRow(const char *label, const std::optional<double> &milliseconds)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", FormatMilliseconds(milliseconds).c_str());
        }

        void DrawCpuPassRow(const char *label,
                            const render::RenderProfilePassMetrics &pass)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f ms", pass.cpu_time_ms);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", FormatMilliseconds(pass.gpu_time_ms).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu/%llu", static_cast<unsigned long long>(pass.draw_calls),
                        static_cast<unsigned long long>(pass.sections));
        }

        std::string BuildClipboardText(
            const RenderProfileSnapshot &profile, const std::optional<double> &shadow,
            const std::optional<double> &lighting, const std::optional<double> &post_process,
            const std::optional<double> &imgui_gpu, const std::optional<double> &total,
            const uint64_t triangle_count, const runtime::Engine::FrameLoopMetrics &frame,
            const double imgui_total_ms, const double imgui_build_ms,
            const double imgui_submit_ms,
            const std::optional<float> &gpu_usage_percent)
        {
            std::ostringstream copied;
            copied << "Performance Profiler\n"
                   << "GPU\n"
                   << "  G-buffer: " << FormatMilliseconds(profile.passes[static_cast<size_t>(
                          RenderProfilePass::GBuffer)]
                          .gpu_time_ms)
                   << "\n"
                   << "  Shadow: " << FormatMilliseconds(shadow) << "\n"
                   << "  Lighting: " << FormatMilliseconds(lighting) << "\n"
                   << "  Postprocess: " << FormatMilliseconds(post_process) << "\n"
                   << "  ImGui GPU: " << FormatMilliseconds(imgui_gpu) << "\n"
                   << "  RT passes: "
                   << FormatMilliseconds(SumPasses(
                          profile, {RenderProfilePass::RayTracingPathTrace,
                                    RenderProfilePass::RayTracingToneMap,
                                    RenderProfilePass::RayTracingBlasBuild,
                                    RenderProfilePass::RayTracingTlasBuild}))
                   << "\n"
                   << "  GPU total: " << FormatMilliseconds(total) << "\n"
                   << "Advanced GPU\n"
                   << "  GPU utilization: " << FormatPercent(gpu_usage_percent) << "\n"
                   << "  GPU timing window: " << profile.gpu_timing_samples
                   << " samples, frame "
                   << (profile.gpu_frame_number.has_value()
                           ? std::to_string(*profile.gpu_frame_number)
                           : "N/A")
                   << "\n"
                   << "  GPU total p50/p95: "
                   << FormatMilliseconds(profile.summary.gpu_total_p50_ms) << " / "
                   << FormatMilliseconds(profile.summary.gpu_total_p95_ms) << "\n"
                   << "  RT samples/dispatch: "
                   << profile.path_trace_samples_per_dispatch << ", bounces: "
                   << profile.path_trace_max_continuation_bounces << "\n"
                   << "  RT geometry/instances/materials/lights: "
                   << profile.ray_tracing_geometry_records << "/"
                   << profile.ray_tracing_instance_records << "/"
                   << profile.ray_tracing_material_records << "/"
                   << profile.ray_tracing_light_records << "\n"
                   << "  AS storage: "
                   << FormatBytes(profile.ray_tracing_acceleration_structure_storage_bytes)
                   << "; BLAS build/update " << profile.ray_tracing_blas_builds << "/"
                   << profile.ray_tracing_blas_updates << "; TLAS build/update "
                   << profile.ray_tracing_tlas_builds << "/"
                   << profile.ray_tracing_tlas_updates << "\n"
                   << "  RT table packed/uploaded: "
                   << profile.ray_tracing_scene_table_records_packed << "/"
                   << profile.ray_tracing_scene_table_records_uploaded
                   << "; address buffers "
                   << profile.ray_tracing_address_table_buffers_created << "; upload "
                   << FormatBytes(profile.ray_tracing_address_table_upload_bytes) << "\n"
                   << "  RT table cache hits/misses: "
                   << profile.ray_tracing_scene_record_cache_hits_total << "/"
                   << profile.ray_tracing_scene_record_cache_misses_total << "\n"
                   << "  Texture resident/source/decoded: "
                   << FormatBytes(profile.textures.resident_bytes) << "/"
                   << FormatBytes(profile.textures.source_bytes) << "/"
                   << FormatBytes(profile.textures.decoded_bytes) << "\n"
                   << "  Hardware counters: external Nsight capture required\n"
                   << "  Shadow cache: hits " << profile.shadow_cache_hits
                   << ", misses " << profile.shadow_cache_misses << "\n"
                   << "  Draw calls: " << profile.draw_calls << "\n"
                   << "  Triangles: " << triangle_count << "\n"
                   << "Ray tracing\n"
                   << "  Path tracing: "
                   << (!profile.path_tracing_enabled
                           ? "disabled"
                           : FeatureStatus(profile.path_tracing_available,
                                           profile.path_trace_active))
                   << ", samples " << profile.path_trace_samples << "\n"
                   << "  Ray-query shadows: "
                   << FeatureStatus(profile.ray_query_shadows_available,
                                    profile.ray_query_shadows_active)
                   << " (included in deferred lighting timing)\n"
                   << "Frame loop\n"
                   << "  Frame: " << frame.frame_total_ms << " ms\n"
                   << "  Game wait: " << frame.game_wait_ms << " ms\n"
                   << "  Render work: " << frame.render_work_ms << " ms\n"
                   << "  Pace: " << frame.frame_pacing_ms << " ms\n"
                   << "  Game work: " << frame.game_tick_work_ms << " ms\n"
                   << "  Game pace: " << frame.game_tick_pacing_ms << " ms\n"
                   << "CPU / ImGui\n"
                   << "  Render CPU: " << profile.cpu_total_ms << " ms\n"
                   << "  Present: " << profile.cpu_present_ms << " ms\n"
                   << "  Begin: " << profile.cpu_backend_begin_ms << " ms\n"
                   << "  Record: " << profile.cpu_record_ms << " ms\n"
                   << "    World snapshot: " << profile.cpu_render_world_snapshot_ms << " ms\n"
                   << "    RT scene prep: " << profile.cpu_ray_tracing_scene_prepare_ms << " ms\n"
                   << "    Graph execute: " << profile.cpu_graph_execute_ms << " ms\n"
                   << "    Other record: " << UnaccountedRecordTime(profile) << " ms\n"
                   << "  Finalize: " << profile.cpu_finalize_ms << " ms\n"
                   << "  ImGui: " << imgui_total_ms << " ms\n"
                   << "  ImGui build: " << imgui_build_ms << " ms\n"
                   << "  ImGui submit: " << imgui_submit_ms << " ms\n"
                   << "Per-pass CPU / GPU\n";
            const auto append_pass = [&copied, &profile](
                                         const char *label, const RenderProfilePass pass)
            {
                const auto &pass_metrics = profile.passes[static_cast<size_t>(pass)];
                copied << "  " << label << ": CPU " << pass_metrics.cpu_time_ms
                       << " ms, GPU " << FormatMilliseconds(pass_metrics.gpu_time_ms)
                       << ", draws " << pass_metrics.draw_calls
                       << ", sections " << pass_metrics.sections << "\n";
            };
            append_pass("Directional shadow", RenderProfilePass::DirectionalShadow);
            append_pass("Spot shadow", RenderProfilePass::SpotShadow);
            append_pass("Point shadow", RenderProfilePass::PointShadow);
            append_pass("G-buffer", RenderProfilePass::GBuffer);
            append_pass("Deferred lighting", RenderProfilePass::DeferredLighting);
            append_pass("Ray tracing path trace", RenderProfilePass::RayTracingPathTrace);
            append_pass("Ray tracing tone map", RenderProfilePass::RayTracingToneMap);
            append_pass("Ray tracing BLAS build", RenderProfilePass::RayTracingBlasBuild);
            append_pass("Ray tracing TLAS build", RenderProfilePass::RayTracingTlasBuild);
            append_pass("Tone map", RenderProfilePass::ToneMap);
            append_pass("Capture view", RenderProfilePass::CaptureView);
            append_pass("Debug view", RenderProfilePass::DebugView);
            append_pass("ImGui composite", RenderProfilePass::EditorComposite);
            return copied.str();
        }

        std::string BuildClipboardJson(
            const RenderProfileSnapshot &profile, const std::optional<double> &shadow,
            const std::optional<double> &lighting, const std::optional<double> &post_process,
            const std::optional<double> &imgui_gpu, const std::optional<double> &total,
            const uint64_t triangle_count, const runtime::Engine::FrameLoopMetrics &frame,
            const double imgui_total_ms, const double imgui_build_ms,
            const double imgui_submit_ms,
            const std::optional<float> &gpu_usage_percent)
        {
            Json result{
                {"schema", "kimpeanut.profiler.v1"},
                {"profile",
                 {{"frame_number", profile.frame_number},
                  {"gpu_frame_number", profile.gpu_frame_number.has_value()
                                            ? Json(*profile.gpu_frame_number)
                                            : Json(nullptr)},
                  {"graphics_api", GraphicsApiName(profile.graphics_api)},
                  {"ray_tracing",
                   {{"path_tracing_enabled", profile.path_tracing_enabled},
                    {"path_tracing_available", profile.path_tracing_available},
                    {"path_trace_active", profile.path_trace_active},
                    {"path_trace_samples", profile.path_trace_samples},
                    {"ray_query_shadows_available", profile.ray_query_shadows_available},
                    {"ray_query_shadows_active", profile.ray_query_shadows_active},
                    {"dedicated_passes_ms",
                     OptionalMilliseconds(SumPasses(
                         profile, {RenderProfilePass::RayTracingPathTrace,
                                   RenderProfilePass::RayTracingToneMap,
                                   RenderProfilePass::RayTracingBlasBuild,
                                   RenderProfilePass::RayTracingTlasBuild}))}}},
                  {"viewport", {{"width", profile.viewport_width},
                                 {"height", profile.viewport_height}}},
                  {"present_mode", profile.present_mode},
                  {"gpu", {{"g_buffer_ms", OptionalMilliseconds(
                                  profile.passes[static_cast<size_t>(RenderProfilePass::GBuffer)]
                                      .gpu_time_ms)},
                            {"shadow_ms", OptionalMilliseconds(shadow)},
                            {"lighting_ms", OptionalMilliseconds(lighting)},
                            {"post_process_ms", OptionalMilliseconds(post_process)},
                            {"imgui_ms", OptionalMilliseconds(imgui_gpu)},
                            {"ray_tracing_passes_ms",
                             OptionalMilliseconds(SumPasses(
                                 profile, {RenderProfilePass::RayTracingPathTrace,
                                           RenderProfilePass::RayTracingToneMap,
                                           RenderProfilePass::RayTracingBlasBuild,
                                           RenderProfilePass::RayTracingTlasBuild}))},
                            {"total_ms", OptionalMilliseconds(total)},
                            {"gpu_usage_percent", gpu_usage_percent.has_value()
                                                       ? Json(*gpu_usage_percent)
                                                       : Json(nullptr)}}},
                  {"advanced_gpu",
                   {{"timing_samples", profile.gpu_timing_samples},
                    {"gpu_frame_number", profile.gpu_frame_number.has_value()
                                             ? Json(*profile.gpu_frame_number)
                                             : Json(nullptr)},
                    {"total_p50_ms", OptionalMilliseconds(profile.summary.gpu_total_p50_ms)},
                    {"total_p95_ms", OptionalMilliseconds(profile.summary.gpu_total_p95_ms)},
                    {"render_graph_mode", profile.render_graph_mode},
                    {"path_trace_samples_per_dispatch",
                     profile.path_trace_samples_per_dispatch},
                    {"path_trace_max_continuation_bounces",
                     profile.path_trace_max_continuation_bounces},
                    {"rt_records", {{"geometry", profile.ray_tracing_geometry_records},
                                     {"instances", profile.ray_tracing_instance_records},
                                     {"materials", profile.ray_tracing_material_records},
                                     {"lights", profile.ray_tracing_light_records}}},
                    {"acceleration_structure",
                     {{"storage_bytes", profile.ray_tracing_acceleration_structure_storage_bytes},
                      {"blas_builds", profile.ray_tracing_blas_builds},
                      {"blas_updates", profile.ray_tracing_blas_updates},
                      {"tlas_builds", profile.ray_tracing_tlas_builds},
                      {"tlas_updates", profile.ray_tracing_tlas_updates}}},
                    {"scene_table", {{"records_packed",
                                      profile.ray_tracing_scene_table_records_packed},
                                     {"records_uploaded",
                                      profile.ray_tracing_scene_table_records_uploaded},
                                     {"address_buffers_created",
                                      profile.ray_tracing_address_table_buffers_created},
                                     {"upload_bytes",
                                      profile.ray_tracing_address_table_upload_bytes},
                                     {"cache_hits",
                                      profile.ray_tracing_scene_record_cache_hits_total},
                                     {"cache_misses",
                                      profile.ray_tracing_scene_record_cache_misses_total}}},
                    {"textures", {{"dependency_count", profile.textures.dependency_count},
                                   {"residency_complete",
                                    profile.textures.tracked_residency_complete},
                                   {"incomplete_count",
                                    profile.textures.tracked_residency_incomplete_count},
                                   {"resident_bytes", profile.textures.resident_bytes},
                                   {"source_bytes", profile.textures.source_bytes},
                                   {"decoded_bytes", profile.textures.decoded_bytes}}},
                    {"vendor_hardware_counters", "external_capture_required"}}},
                  {"cpu", {{"total_ms", profile.cpu_total_ms},
                            {"scene_prepare_ms", profile.cpu_scene_prepare_ms},
                            {"backend_begin_ms", profile.cpu_backend_begin_ms},
                            {"record_ms", profile.cpu_record_ms},
                            {"record_breakdown_ms",
                             {{"world_snapshot", profile.cpu_render_world_snapshot_ms},
                              {"ray_tracing_scene_prepare",
                               profile.cpu_ray_tracing_scene_prepare_ms},
                              {"graph_execute", profile.cpu_graph_execute_ms},
                              {"other", UnaccountedRecordTime(profile)}}},
                            {"finalize_ms", profile.cpu_finalize_ms},
                            {"present_ms", profile.cpu_present_ms},
                            {"imgui_total_ms", imgui_total_ms},
                            {"imgui_build_ms", imgui_build_ms},
                            {"imgui_submit_ms", imgui_submit_ms}}},
                  {"frame_loop", {{"frame_total_ms", frame.frame_total_ms},
                                   {"game_wait_ms", frame.game_wait_ms},
                                   {"render_work_ms", frame.render_work_ms},
                                   {"frame_pacing_ms", frame.frame_pacing_ms},
                                   {"game_tick_work_ms", frame.game_tick_work_ms},
                                   {"game_tick_pacing_ms", frame.game_tick_pacing_ms}}},
                  {"geometry", {{"draw_calls", profile.draw_calls},
                                 {"sections", profile.sections},
                                 {"triangles", triangle_count}}},
                  {"shadow_cache", {{"hits", profile.shadow_cache_hits},
                                     {"misses", profile.shadow_cache_misses}}},
                  {"descriptors", {{"sets_created", profile.descriptor_sets_created},
                                    {"pools_created", profile.descriptor_pools_created},
                                    {"search_cpu_ms", profile.descriptor_search_cpu_ms},
                                    {"allocation_cpu_ms", profile.descriptor_allocation_cpu_ms},
                                    {"update_cpu_ms", profile.descriptor_update_cpu_ms},
                                    {"searches", profile.descriptor_searches},
                                    {"allocations", profile.descriptor_allocations},
                                    {"updates", profile.descriptor_updates}}},
                  {"section_packets", {{"build_cpu_ms", profile.cpu_section_packet_build_ms},
                                        {"build_calls", profile.section_packet_build_calls},
                                        {"built", profile.section_packets_built}}},
                  {"shadow_stamping", {{"fit_cpu_ms", profile.cpu_shadow_stamp_fit_ms},
                                        {"stamp_evaluations", profile.shadow_stamp_evaluations},
                                        {"fit_evaluations", profile.shadow_fit_evaluations}}},
                  {"materials", {{"resolution_cpu_ms", profile.cpu_material_resolution_ms},
                                   {"resolution_calls", profile.material_resolution_calls}}},
                  {"uniforms", {{"write_cpu_ms", profile.cpu_uniform_write_ms},
                                 {"writes", profile.uniform_writes},
                                 {"write_bytes", profile.uniform_write_bytes}}},
                  {"recorder", {{"pipeline_validation_cpu_ms",
                                  profile.pipeline_validation_cpu_ms},
                                 {"pipeline_validation_calls",
                                  profile.pipeline_validation_calls},
                                 {"pipeline_bind_requests", profile.pipeline_bind_requests},
                                 {"pipeline_bind_emitted", profile.pipeline_bind_emitted},
                                 {"mesh_bind_requests", profile.mesh_bind_requests},
                                 {"mesh_bind_emitted", profile.mesh_bind_emitted},
                                 {"resource_binding_bind_requests",
                                  profile.resource_binding_bind_requests},
                                 {"resource_binding_bind_emitted",
                                  profile.resource_binding_bind_emitted},
                                 {"native_draw_calls", profile.native_draw_calls}}},
                  {"textures", {{"dependency_count", profile.textures.dependency_count},
                                 {"source_bytes", profile.textures.source_bytes},
                                 {"decoded_bytes", profile.textures.decoded_bytes},
                                 {"resident_bytes", profile.textures.resident_bytes}}}}},
            };

            Json &passes = result["passes"] = Json::array();
            for (size_t index = 0;
                 index < static_cast<size_t>(RenderProfilePass::Count); ++index)
            {
                const auto &pass = profile.passes[index];
                passes.push_back({{"name", RenderPassName(static_cast<RenderProfilePass>(index))},
                                  {"cpu_ms", pass.cpu_time_ms},
                                  {"gpu_ms", OptionalMilliseconds(pass.gpu_time_ms)},
                                  {"draw_calls", pass.draw_calls},
                                  {"sections", pass.sections}});
            }

            Json &summary = result["summary"] = {
                {"complete", profile.summary.complete},
                {"warmup_frames_completed", profile.summary.warmup_frames_completed},
                {"samples_collected", profile.summary.samples_collected},
                {"cpu_total_p50_ms", profile.summary.cpu_total_p50_ms},
                {"cpu_total_p95_ms", profile.summary.cpu_total_p95_ms},
                {"cpu_present_p50_ms", profile.summary.cpu_present_p50_ms},
                {"cpu_present_p95_ms", profile.summary.cpu_present_p95_ms},
            };
            summary["passes"] = Json::array();
            for (size_t index = 0;
                 index < static_cast<size_t>(RenderProfilePass::Count); ++index)
            {
                const auto &pass = profile.summary.passes[index];
                summary["passes"].push_back(
                    {{"name", RenderPassName(static_cast<RenderProfilePass>(index))},
                     {"gpu_p50_ms", OptionalMilliseconds(pass.gpu_p50_ms)},
                     {"gpu_p95_ms", OptionalMilliseconds(pass.gpu_p95_ms)}});
            }

            summary["cpu_subphases"] = Json::array();
            for (size_t index = 0;
                 index < static_cast<size_t>(render::RenderProfileCpuSubphase::Count); ++index)
            {
                const auto &subphase = profile.summary.cpu_subphases[index];
                summary["cpu_subphases"].push_back(
                    {{"name", CpuSubphaseName(
                                  static_cast<render::RenderProfileCpuSubphase>(index))},
                     {"cpu_p50_ms", OptionalMilliseconds(subphase.cpu_p50_ms)},
                     {"cpu_p95_ms", OptionalMilliseconds(subphase.cpu_p95_ms)}});
            }

            return result.dump(2);
        }
    }

    EditorGpuProfilerComponent::EditorGpuProfilerComponent(runtime::Engine *engine,
                                                           render::RenderSystem *render_system,
                                                           const EditorUI *editor_ui)
        // No layout slot: this panel is hosted by the tool row, which draws its body
        // inside the row's own window. A slot here would put it in the per-frame layout
        // pass as well and render it twice.
        : EditorWindowComponent("Performance Profiler", EditorWindowConfig{}),
          engine_(engine),
          render_system_(render_system),
          editor_ui_(editor_ui)
    {
    }

    void EditorGpuProfilerComponent::RenderContent()
    {
        if (render_system_ == nullptr)
        {
            ImGui::TextDisabled("GPU profile unavailable");
            return;
        }

        const render::RenderSystem::RenderSystemMetrics metrics = render_system_->GetMetrics();
        const RenderProfileSnapshot &profile = metrics.profile;
        const std::optional<double> shadow = SumPasses(
            profile, {RenderProfilePass::DirectionalShadow, RenderProfilePass::SpotShadow,
                      RenderProfilePass::PointShadow});
        const std::optional<double> lighting =
            SumPasses(profile, {RenderProfilePass::DeferredLighting});
        const std::optional<double> post_process =
            SumPasses(profile, {RenderProfilePass::ToneMap, RenderProfilePass::CaptureView,
                                RenderProfilePass::DebugView});
        const std::optional<double> imgui_gpu =
            SumPasses(profile, {RenderProfilePass::EditorComposite});
        const std::optional<double> total = SumPasses(
            profile, {RenderProfilePass::DirectionalShadow, RenderProfilePass::SpotShadow,
                      RenderProfilePass::PointShadow, RenderProfilePass::GBuffer,
                      RenderProfilePass::DeferredLighting, RenderProfilePass::ToneMap,
                      RenderProfilePass::CaptureView, RenderProfilePass::DebugView,
                      RenderProfilePass::EditorComposite,
                      RenderProfilePass::RayTracingPathTrace,
                      RenderProfilePass::RayTracingToneMap,
                      RenderProfilePass::RayTracingBlasBuild,
                      RenderProfilePass::RayTracingTlasBuild});
        const std::optional<double> ray_tracing = SumPasses(
            profile, {RenderProfilePass::RayTracingPathTrace,
                      RenderProfilePass::RayTracingToneMap,
                      RenderProfilePass::RayTracingBlasBuild,
                      RenderProfilePass::RayTracingTlasBuild});

        if (ImGui::BeginTable("##GpuProfilerStages", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            DrawStageRow("G-buffer", profile.passes[static_cast<size_t>(
                                           RenderProfilePass::GBuffer)]
                                           .gpu_time_ms);
            DrawStageRow("Shadow", shadow);
            DrawStageRow("Lighting", lighting);
            DrawStageRow("Postprocess", post_process);
            DrawStageRow("ImGui GPU", imgui_gpu);
            DrawStageRow("RT passes", ray_tracing);
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Ray tracing");
        ImGui::Text("Path tracing %s  Samples %u",
                    !profile.path_tracing_enabled
                        ? "disabled"
                        : FeatureStatus(profile.path_tracing_available,
                                        profile.path_trace_active),
                    profile.path_trace_samples);
        ImGui::Text("Ray-query shadows %s  (timed with deferred lighting)",
                    FeatureStatus(profile.ray_query_shadows_available,
                                  profile.ray_query_shadows_active));

        const runtime::Engine::FrameLoopMetrics frame =
            engine_ != nullptr ? engine_->GetFrameLoopMetrics()
                               : runtime::Engine::FrameLoopMetrics{};
        const double imgui_total_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastRenderTimeMs() : 0.0;
        const double imgui_build_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastImGuiBuildTimeMs() : 0.0;
        const double imgui_submit_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastImGuiSubmitTimeMs() : 0.0;

        if (ImGui::CollapsingHeader("Advanced GPU"))
        {
            if (ImGui::BeginTable("##AdvancedGpuDetails", 2,
                                  ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_NoBordersInBody))
            {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                DrawInfoRow("GPU utilization", FormatPercent(metrics.gpu_usage_percent));
                DrawInfoRow("GPU timing samples", std::to_string(profile.gpu_timing_samples));
                DrawInfoRow("GPU query frame",
                            profile.gpu_frame_number.has_value()
                                ? std::to_string(*profile.gpu_frame_number)
                                : "N/A");
                DrawInfoRow("GPU total p50 / p95",
                            FormatMilliseconds(profile.summary.gpu_total_p50_ms) + " / " +
                                FormatMilliseconds(profile.summary.gpu_total_p95_ms));
                DrawInfoRow("Render graph", profile.render_graph_mode);
                DrawInfoRow("PT samples / dispatch, bounces",
                            std::to_string(profile.path_trace_samples_per_dispatch) + " / " +
                                std::to_string(profile.path_trace_max_continuation_bounces));
                DrawInfoRow("RT records (geom / inst / mat / light)",
                            std::to_string(profile.ray_tracing_geometry_records) + " / " +
                                std::to_string(profile.ray_tracing_instance_records) + " / " +
                                std::to_string(profile.ray_tracing_material_records) + " / " +
                                std::to_string(profile.ray_tracing_light_records));
                DrawInfoRow("AS storage",
                            FormatBytes(profile.ray_tracing_acceleration_structure_storage_bytes));
                DrawInfoRow("BLAS build / update",
                            std::to_string(profile.ray_tracing_blas_builds) + " / " +
                                std::to_string(profile.ray_tracing_blas_updates));
                DrawInfoRow("TLAS build / update",
                            std::to_string(profile.ray_tracing_tlas_builds) + " / " +
                                std::to_string(profile.ray_tracing_tlas_updates));
                DrawInfoRow("RT table packed / uploaded",
                            std::to_string(profile.ray_tracing_scene_table_records_packed) +
                                " / " + std::to_string(
                                            profile.ray_tracing_scene_table_records_uploaded));
                DrawInfoRow("Address table buffers / upload",
                            std::to_string(profile.ray_tracing_address_table_buffers_created) +
                                " / " + FormatBytes(
                                            profile.ray_tracing_address_table_upload_bytes));
                DrawInfoRow("Scene-table cache hits / misses",
                            std::to_string(profile.ray_tracing_scene_record_cache_hits_total) +
                                " / " + std::to_string(
                                            profile.ray_tracing_scene_record_cache_misses_total));
                DrawInfoRow("Texture resident / source / decoded",
                            FormatBytes(profile.textures.resident_bytes) + " / " +
                                FormatBytes(profile.textures.source_bytes) + " / " +
                                FormatBytes(profile.textures.decoded_bytes));
                DrawInfoRow("Texture residency tracking",
                            profile.textures.tracked_residency_complete
                                ? "complete"
                                : "incomplete (" + std::to_string(
                                                       profile.textures
                                                           .tracked_residency_incomplete_count) +
                                      ")");
                ImGui::EndTable();
            }
            ImGui::TextWrapped(
                "Registers, spills, occupancy, cache throughput, traversal utilization, "
                "and per-ray counts are not collected by engine telemetry; use an external "
                "Nsight Graphics capture for those counters.");
        }

        if (ImGui::Button("Copy profiler JSON"))
        {
            const std::string copied = BuildClipboardJson(
                profile, shadow, lighting, post_process, imgui_gpu, total,
                metrics.triangle_count, frame, imgui_total_ms, imgui_build_ms, imgui_submit_ms,
                metrics.gpu_usage_percent);
            ImGui::SetClipboardText(copied.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy profiler text"))
        {
            const std::string copied = BuildClipboardText(
                profile, shadow, lighting, post_process, imgui_gpu, total,
                metrics.triangle_count, frame, imgui_total_ms, imgui_build_ms, imgui_submit_ms,
                metrics.gpu_usage_percent);
            ImGui::SetClipboardText(copied.c_str());
        }

        ImGui::Separator();
        ImGui::Text("GPU total  %s", FormatMilliseconds(total).c_str());
        ImGui::Text("Shadow cache  hits %llu  misses %llu",
                    static_cast<unsigned long long>(profile.shadow_cache_hits),
                    static_cast<unsigned long long>(profile.shadow_cache_misses));
        ImGui::Text("Draw calls %llu", static_cast<unsigned long long>(profile.draw_calls));
        ImGui::Text("Triangles  %llu",
                    static_cast<unsigned long long>(metrics.triangle_count));

        ImGui::Separator();
        ImGui::TextDisabled("Frame loop");
        ImGui::Text("Frame %.2f ms  Game wait %.2f ms", frame.frame_total_ms,
                    frame.game_wait_ms);
        ImGui::Text("Render work %.2f ms  Pace %.2f ms", frame.render_work_ms,
                    frame.frame_pacing_ms);
        ImGui::Text("Game work %.2f ms  Game pace %.2f ms", frame.game_tick_work_ms,
                    frame.game_tick_pacing_ms);

        ImGui::Separator();
        ImGui::TextDisabled("CPU / ImGui");
        char value[128]{};
        std::snprintf(value, sizeof(value), "Render CPU %.2f ms  Present %.2f ms",
                      profile.cpu_total_ms, profile.cpu_present_ms);
        ImGui::Text("%s", value);
        std::snprintf(value, sizeof(value), "Begin %.2f  Record %.2f  Finalize %.2f ms",
                      profile.cpu_backend_begin_ms, profile.cpu_record_ms,
                      profile.cpu_finalize_ms);
        ImGui::Text("%s", value);
        ImGui::Text("Record detail  Snapshot %.2f  RT scene %.2f  Graph %.2f  Other %.2f ms",
                    profile.cpu_render_world_snapshot_ms,
                    profile.cpu_ray_tracing_scene_prepare_ms,
                    profile.cpu_graph_execute_ms, UnaccountedRecordTime(profile));
        std::snprintf(value, sizeof(value), "ImGui %.2f ms  (build %.2f / submit %.2f)",
                      imgui_total_ms, imgui_build_ms, imgui_submit_ms);
        ImGui::Text("%s", value);

        if (ImGui::CollapsingHeader("Per-pass CPU / GPU", ImGuiTreeNodeFlags_DefaultOpen) &&
            ImGui::BeginTable("##ProfilerPassDetails", 4,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoBordersInBody))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("D/S", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            const auto &passes = profile.passes;
            DrawCpuPassRow("Directional shadow",
                           passes[static_cast<size_t>(RenderProfilePass::DirectionalShadow)]);
            DrawCpuPassRow("Spot shadow",
                           passes[static_cast<size_t>(RenderProfilePass::SpotShadow)]);
            DrawCpuPassRow("Point shadow",
                           passes[static_cast<size_t>(RenderProfilePass::PointShadow)]);
            DrawCpuPassRow("G-buffer", passes[static_cast<size_t>(RenderProfilePass::GBuffer)]);
            DrawCpuPassRow("Deferred lighting",
                           passes[static_cast<size_t>(RenderProfilePass::DeferredLighting)]);
            DrawCpuPassRow("RT path trace",
                           passes[static_cast<size_t>(RenderProfilePass::RayTracingPathTrace)]);
            DrawCpuPassRow("RT tone map",
                           passes[static_cast<size_t>(RenderProfilePass::RayTracingToneMap)]);
            DrawCpuPassRow("RT BLAS build",
                           passes[static_cast<size_t>(RenderProfilePass::RayTracingBlasBuild)]);
            DrawCpuPassRow("RT TLAS build",
                           passes[static_cast<size_t>(RenderProfilePass::RayTracingTlasBuild)]);
            DrawCpuPassRow("Tone map", passes[static_cast<size_t>(RenderProfilePass::ToneMap)]);
            DrawCpuPassRow("Capture view",
                           passes[static_cast<size_t>(RenderProfilePass::CaptureView)]);
            DrawCpuPassRow("Debug view",
                           passes[static_cast<size_t>(RenderProfilePass::DebugView)]);
            DrawCpuPassRow("ImGui composite",
                           passes[static_cast<size_t>(RenderProfilePass::EditorComposite)]);
            ImGui::EndTable();
        }

    }
}
