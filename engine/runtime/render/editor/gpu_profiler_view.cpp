#include "gpu_profiler_view.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "render/render_profile.h"
#include "render/render_system.h"

namespace kpengine::render::editor
{
    namespace
    {
        using Json = nlohmann::json;
        using kpengine::editor::ui::UiContext;
        using kpengine::editor::ui::UiTableColumn;
        using kpengine::editor::ui::UiTextRole;

        const char *GraphicsApiName(const GraphicsAPIType api)
        {
            switch (api)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL: return "opengl";
            case GraphicsAPIType::GRAPHICS_API_VULKAN: return "vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW: return "unknown";
            }
            return "unknown";
        }

        const char *CpuSubphaseName(const RenderProfileCpuSubphase subphase)
        {
            switch (subphase)
            {
            case RenderProfileCpuSubphase::SectionPacketBuild: return "section_packet_build";
            case RenderProfileCpuSubphase::ShadowStampFit: return "shadow_stamp_fit";
            case RenderProfileCpuSubphase::MaterialResolution: return "material_resolution";
            case RenderProfileCpuSubphase::UniformWrite: return "uniform_write";
            case RenderProfileCpuSubphase::DescriptorSearch: return "descriptor_search";
            case RenderProfileCpuSubphase::DescriptorAllocation: return "descriptor_allocation";
            case RenderProfileCpuSubphase::DescriptorUpdate: return "descriptor_update";
            case RenderProfileCpuSubphase::PipelineValidation: return "pipeline_validation";
            case RenderProfileCpuSubphase::GraphExecute: return "graph_execute";
            case RenderProfileCpuSubphase::Count: return "unknown";
            }
            return "unknown";
        }

        constexpr std::array kPasses{
            RenderProfilePass::DirectionalShadow, RenderProfilePass::SpotShadow,
            RenderProfilePass::PointShadow, RenderProfilePass::GBuffer,
            RenderProfilePass::DeferredLighting, RenderProfilePass::RayTracingPathTrace,
            RenderProfilePass::RayTracingToneMap, RenderProfilePass::RayTracingBlasBuild,
            RenderProfilePass::RayTracingTlasBuild, RenderProfilePass::ToneMap,
            RenderProfilePass::CaptureView, RenderProfilePass::DebugView,
            RenderProfilePass::EditorComposite};

        const char *PassName(const RenderProfilePass pass)
        {
            switch (pass)
            {
            case RenderProfilePass::DirectionalShadow: return "Directional shadow";
            case RenderProfilePass::SpotShadow: return "Spot shadow";
            case RenderProfilePass::PointShadow: return "Point shadow";
            case RenderProfilePass::GBuffer: return "G-buffer";
            case RenderProfilePass::DeferredLighting: return "Deferred lighting";
            case RenderProfilePass::RayTracingPathTrace: return "RT path trace";
            case RenderProfilePass::RayTracingToneMap: return "RT tone map";
            case RenderProfilePass::RayTracingBlasBuild: return "RT BLAS build";
            case RenderProfilePass::RayTracingTlasBuild: return "RT TLAS build";
            case RenderProfilePass::ToneMap: return "Tone map";
            case RenderProfilePass::CaptureView: return "Capture view";
            case RenderProfilePass::DebugView: return "Debug view";
            case RenderProfilePass::EditorComposite: return "Editor composite";
            case RenderProfilePass::Count: return "Unknown";
            }
            return "Unknown";
        }

        std::string Milliseconds(const std::optional<double> value)
        {
            if (!value) return "N/A";
            char buffer[48]{};
            std::snprintf(buffer, sizeof(buffer), "%.2f ms", *value);
            return buffer;
        }

        std::string Percent(const std::optional<float> value)
        {
            if (!value) return "N/A";
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%.1f%%", static_cast<double>(*value));
            return buffer;
        }

        std::string Bytes(const std::uint64_t value)
        {
            char buffer[48]{};
            std::snprintf(buffer, sizeof(buffer), "%.2f MiB",
                          static_cast<double>(value) / (1024.0 * 1024.0));
            return buffer;
        }

        Json Optional(const std::optional<double> value)
        {
            return value ? Json(*value) : Json(nullptr);
        }

        std::optional<double> SumPasses(const RenderProfileSnapshot &profile,
                                        const std::initializer_list<RenderProfilePass> passes)
        {
            double total = 0.0;
            bool has_sample = false;
            for (const RenderProfilePass pass : passes)
            {
                const auto &sample = profile.passes[static_cast<std::size_t>(pass)].gpu_time_ms;
                if (!sample) continue;
                total += *sample;
                has_sample = true;
            }
            return has_sample ? std::optional<double>{total} : std::nullopt;
        }

        std::string BuildText(const RenderSystem::RenderSystemMetrics &metrics,
                              const SceneEditorTimingSnapshot &timings)
        {
            const RenderProfileSnapshot &profile = metrics.profile;
            std::string text = "Performance Profiler\n";
            text += "frame=" + std::to_string(profile.frame_number) + "\n";
            text += "gpu_frame=" + (profile.gpu_frame_number
                ? std::to_string(*profile.gpu_frame_number) : "N/A") + "\n";
            text += "gpu_samples=" + std::to_string(profile.gpu_timing_samples) + "\n";
            text += "gpu_total_p50=" + Milliseconds(profile.summary.gpu_total_p50_ms) + "\n";
            text += "gpu_total_p95=" + Milliseconds(profile.summary.gpu_total_p95_ms) + "\n";
            text += "render_cpu_ms=" + std::to_string(profile.cpu_total_ms) + "\n";
            text += "frame_loop_ms=" + std::to_string(timings.frame_total_ms) + "\n";
            text += "ui_total_ms=" + std::to_string(timings.ui_total_ms) + "\n";
            text += "ui_build_ms=" + std::to_string(timings.ui_build_ms) + "\n";
            text += "ui_submit_ms=" + std::to_string(timings.ui_submit_ms) + "\n";
            text += "draw_calls=" + std::to_string(profile.draw_calls) + "\n";
            text += "triangles=" + std::to_string(metrics.triangle_count) + "\n";
            return text;
        }

        Json BuildJson(const RenderSystem::RenderSystemMetrics &metrics,
                       const SceneEditorTimingSnapshot &timings)
        {
            const RenderProfileSnapshot &profile = metrics.profile;
            const auto shadow = SumPasses(profile, {RenderProfilePass::DirectionalShadow,
                RenderProfilePass::SpotShadow, RenderProfilePass::PointShadow});
            const auto lighting = SumPasses(profile, {RenderProfilePass::DeferredLighting});
            const auto post_process = SumPasses(profile, {RenderProfilePass::ToneMap,
                RenderProfilePass::CaptureView, RenderProfilePass::DebugView});
            const auto editor_composite = SumPasses(profile, {RenderProfilePass::EditorComposite});
            const auto ray_tracing = SumPasses(profile, {RenderProfilePass::RayTracingPathTrace,
                RenderProfilePass::RayTracingToneMap, RenderProfilePass::RayTracingBlasBuild,
                RenderProfilePass::RayTracingTlasBuild});
            const auto total = SumPasses(profile, {RenderProfilePass::DirectionalShadow,
                RenderProfilePass::SpotShadow, RenderProfilePass::PointShadow,
                RenderProfilePass::GBuffer, RenderProfilePass::DeferredLighting,
                RenderProfilePass::ToneMap, RenderProfilePass::CaptureView,
                RenderProfilePass::DebugView, RenderProfilePass::EditorComposite,
                RenderProfilePass::RayTracingPathTrace, RenderProfilePass::RayTracingToneMap,
                RenderProfilePass::RayTracingBlasBuild, RenderProfilePass::RayTracingTlasBuild});
            Json passes = Json::array();
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(RenderProfilePass::Count); ++index)
            {
                const RenderProfilePassMetrics &sample =
                    profile.passes[index];
                passes.push_back({{"name", PassName(static_cast<RenderProfilePass>(index))},
                                  {"cpu_ms", sample.cpu_time_ms},
                                  {"gpu_ms", Optional(sample.gpu_time_ms)},
                                  {"draw_calls", sample.draw_calls},
                                  {"sections", sample.sections}});
            }
            Json summary{
                {"complete", profile.summary.complete},
                {"warmup_frames_completed", profile.summary.warmup_frames_completed},
                {"samples_collected", profile.summary.samples_collected},
                {"cpu_total_p50_ms", profile.summary.cpu_total_p50_ms},
                {"cpu_total_p95_ms", profile.summary.cpu_total_p95_ms},
                {"cpu_present_p50_ms", profile.summary.cpu_present_p50_ms},
                {"cpu_present_p95_ms", profile.summary.cpu_present_p95_ms}};
            Json result{
                {"schema", "kimpeanut.profiler.v1"},
                {"profile", {{"frame_number", profile.frame_number},
                    {"gpu_frame_number", profile.gpu_frame_number
                        ? Json(*profile.gpu_frame_number) : Json(nullptr)},
                    {"graphics_api", GraphicsApiName(profile.graphics_api)},
                    {"viewport", {{"width", profile.viewport_width},
                                  {"height", profile.viewport_height}}},
                    {"present_mode", profile.present_mode}}},
                {"gpu", {{"g_buffer_ms", Optional(profile.passes[
                        static_cast<std::size_t>(RenderProfilePass::GBuffer)].gpu_time_ms)},
                    {"shadow_ms", Optional(shadow)}, {"lighting_ms", Optional(lighting)},
                    {"post_process_ms", Optional(post_process)},
                    {"imgui_ms", Optional(editor_composite)},
                    {"ray_tracing_passes_ms", Optional(ray_tracing)},
                    {"total_ms", Optional(total)}, {"gpu_usage_percent",
                        metrics.gpu_usage_percent ? Json(*metrics.gpu_usage_percent) : Json(nullptr)}}},
                {"advanced_gpu", {{"timing_samples", profile.gpu_timing_samples},
                    {"total_p50_ms", Optional(profile.summary.gpu_total_p50_ms)},
                    {"total_p95_ms", Optional(profile.summary.gpu_total_p95_ms)},
                    {"render_graph_mode", profile.render_graph_mode},
                    {"path_trace_samples_per_dispatch", profile.path_trace_samples_per_dispatch},
                    {"path_trace_max_continuation_bounces",
                     profile.path_trace_max_continuation_bounces},
                    {"rt_records", {{"geometry", profile.ray_tracing_geometry_records},
                        {"instances", profile.ray_tracing_instance_records},
                        {"materials", profile.ray_tracing_material_records},
                        {"lights", profile.ray_tracing_light_records}}},
                    {"acceleration_structure", {{"storage_bytes",
                        profile.ray_tracing_acceleration_structure_storage_bytes},
                        {"blas_builds", profile.ray_tracing_blas_builds},
                        {"blas_updates", profile.ray_tracing_blas_updates},
                        {"tlas_builds", profile.ray_tracing_tlas_builds},
                        {"tlas_updates", profile.ray_tracing_tlas_updates}}},
                    {"scene_table", {{"records_packed",
                        profile.ray_tracing_scene_table_records_packed},
                        {"records_uploaded", profile.ray_tracing_scene_table_records_uploaded},
                        {"address_buffers_created",
                         profile.ray_tracing_address_table_buffers_created},
                        {"upload_bytes", profile.ray_tracing_address_table_upload_bytes},
                        {"cache_hits", profile.ray_tracing_scene_record_cache_hits_total},
                        {"cache_misses", profile.ray_tracing_scene_record_cache_misses_total}}},
                    {"textures", {{"dependency_count", profile.textures.dependency_count},
                        {"residency_complete", profile.textures.tracked_residency_complete},
                        {"incomplete_count", profile.textures.tracked_residency_incomplete_count},
                        {"resident_bytes", profile.textures.resident_bytes},
                        {"source_bytes", profile.textures.source_bytes},
                        {"decoded_bytes", profile.textures.decoded_bytes}}},
                    {"vendor_hardware_counters", "external_capture_required"}}},
                {"cpu", {{"total_ms", profile.cpu_total_ms},
                    {"scene_prepare_ms", profile.cpu_scene_prepare_ms},
                    {"backend_begin_ms", profile.cpu_backend_begin_ms},
                    {"record_ms", profile.cpu_record_ms},
                    {"record_breakdown_ms", {{"world_snapshot",
                        profile.cpu_render_world_snapshot_ms}, {"ray_tracing_scene_prepare",
                        profile.cpu_ray_tracing_scene_prepare_ms}, {"graph_execute",
                        profile.cpu_graph_execute_ms}, {"other", std::max(0.0,
                            profile.cpu_record_ms - profile.cpu_render_world_snapshot_ms -
                            profile.cpu_ray_tracing_scene_prepare_ms - profile.cpu_graph_execute_ms)}}},
                    {"finalize_ms", profile.cpu_finalize_ms},
                    {"present_ms", profile.cpu_present_ms},
                    {"imgui_total_ms", timings.ui_total_ms},
                    {"imgui_build_ms", timings.ui_build_ms},
                    {"imgui_submit_ms", timings.ui_submit_ms}}},
                {"frame_loop", {{"frame_total_ms", timings.frame_total_ms},
                    {"game_wait_ms", timings.game_wait_ms}, {"render_work_ms", timings.render_work_ms},
                    {"frame_pacing_ms", timings.frame_pacing_ms},
                    {"game_tick_work_ms", timings.game_tick_work_ms},
                    {"game_tick_pacing_ms", timings.game_tick_pacing_ms}}},
                {"geometry", {{"draw_calls", profile.draw_calls}, {"sections", profile.sections},
                              {"triangles", metrics.triangle_count}}},
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
                {"recorder", {{"pipeline_validation_cpu_ms", profile.pipeline_validation_cpu_ms},
                    {"pipeline_validation_calls", profile.pipeline_validation_calls},
                    {"pipeline_bind_requests", profile.pipeline_bind_requests},
                    {"pipeline_bind_emitted", profile.pipeline_bind_emitted},
                    {"mesh_bind_requests", profile.mesh_bind_requests},
                    {"mesh_bind_emitted", profile.mesh_bind_emitted},
                    {"resource_binding_bind_requests", profile.resource_binding_bind_requests},
                    {"resource_binding_bind_emitted", profile.resource_binding_bind_emitted},
                    {"native_draw_calls", profile.native_draw_calls}}},
                {"textures", {{"dependency_count", profile.textures.dependency_count},
                    {"source_bytes", profile.textures.source_bytes},
                    {"decoded_bytes", profile.textures.decoded_bytes},
                    {"resident_bytes", profile.textures.resident_bytes}}},
                {"passes", std::move(passes)}, {"summary", std::move(summary)}};
            Json &summary_passes = result["summary"]["passes"] = Json::array();
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(RenderProfilePass::Count); ++index)
            {
                const auto &sample = profile.summary.passes[index];
                summary_passes.push_back({
                    {"name", PassName(static_cast<RenderProfilePass>(index))},
                    {"gpu_p50_ms", Optional(sample.gpu_p50_ms)},
                    {"gpu_p95_ms", Optional(sample.gpu_p95_ms)}});
            }
            Json &cpu_subphases = result["summary"]["cpu_subphases"] = Json::array();
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(RenderProfileCpuSubphase::Count); ++index)
            {
                const auto &sample = profile.summary.cpu_subphases[index];
                cpu_subphases.push_back({
                    {"name", CpuSubphaseName(static_cast<RenderProfileCpuSubphase>(index))},
                    {"cpu_p50_ms", Optional(sample.cpu_p50_ms)},
                    {"cpu_p95_ms", Optional(sample.cpu_p95_ms)}});
            }
            return result;
        }

        void Row(UiContext &ui, const std::string_view name, const std::string &value)
        {
            ui.TableRow();
            ui.TableColumn();
            ui.Text(name, UiTextRole::Muted);
            ui.TableColumn();
            ui.Text(value);
        }
    }

    GpuProfilerView::GpuProfilerView(RenderSystem *const render_system) noexcept
        : render_system_(render_system)
    {
    }

    void GpuProfilerView::Draw(UiContext &ui, const SceneEditorTimingSnapshot &timings) const
    {
        if (render_system_ == nullptr)
        {
            ui.Text("Render performance metrics are unavailable.", UiTextRole::Muted);
            return;
        }

        const RenderSystem::RenderSystemMetrics metrics = render_system_->GetPublishedMetrics();
        const RenderProfileSnapshot &profile = metrics.profile;
        ui.Section("GPU");
        const UiTableColumn summary_columns[]{{"Metric", 150.0F}, {"Value", 0.0F}};
        ui.Table("render.profiler.summary", summary_columns, 0.0F, false, [&]
        {
            Row(ui, "GPU frame", profile.gpu_frame_number
                ? std::to_string(*profile.gpu_frame_number) : "N/A");
            Row(ui, "GPU timing samples", std::to_string(profile.gpu_timing_samples));
            Row(ui, "GPU total p50 / p95",
                Milliseconds(profile.summary.gpu_total_p50_ms) + " / " +
                    Milliseconds(profile.summary.gpu_total_p95_ms));
            Row(ui, "GPU utilization", Percent(metrics.gpu_usage_percent));
            Row(ui, "Viewport", std::to_string(profile.viewport_width) + " x " +
                                   std::to_string(profile.viewport_height));
            Row(ui, "Draw calls / triangles", std::to_string(profile.draw_calls) + " / " +
                                               std::to_string(metrics.triangle_count));
        });

        ui.Section("Frame and UI CPU");
        ui.Table("render.profiler.cpu", summary_columns, 0.0F, false, [&]
        {
            Row(ui, "Frame", Milliseconds(timings.frame_total_ms));
            Row(ui, "Game wait", Milliseconds(timings.game_wait_ms));
            Row(ui, "Render work", Milliseconds(timings.render_work_ms));
            Row(ui, "Frame pacing", Milliseconds(timings.frame_pacing_ms));
            Row(ui, "Game work / pacing", Milliseconds(timings.game_tick_work_ms) + " / " +
                                                Milliseconds(timings.game_tick_pacing_ms));
            Row(ui, "Render CPU / present", Milliseconds(profile.cpu_total_ms) + " / " +
                                                Milliseconds(profile.cpu_present_ms));
            Row(ui, "UI build / submit", Milliseconds(timings.ui_build_ms) + " / " +
                                             Milliseconds(timings.ui_submit_ms));
            Row(ui, "UI total", Milliseconds(timings.ui_total_ms));
            Row(ui, "Begin / record / finalize", Milliseconds(profile.cpu_backend_begin_ms) +
                " / " + Milliseconds(profile.cpu_record_ms) + " / " +
                Milliseconds(profile.cpu_finalize_ms));
            Row(ui, "Record: scene / world snapshot", Milliseconds(profile.cpu_scene_prepare_ms) +
                " / " + Milliseconds(profile.cpu_render_world_snapshot_ms));
            Row(ui, "Record: RT prepare / graph execute",
                Milliseconds(profile.cpu_ray_tracing_scene_prepare_ms) + " / " +
                Milliseconds(profile.cpu_graph_execute_ms));
            Row(ui, "Graph compilation", Milliseconds(profile.graph_compile_ms));
            Row(ui, "Present waits: fence / acquire", Milliseconds(profile.cpu_fence_wait_ms) +
                " / " + Milliseconds(profile.cpu_acquire_wait_ms));
            Row(ui, "Queue present", Milliseconds(profile.cpu_queue_present_ms));
            Row(ui, "Recorder: section packets / shadow fit",
                Milliseconds(profile.cpu_section_packet_build_ms) + " / " +
                Milliseconds(profile.cpu_shadow_stamp_fit_ms));
            Row(ui, "Recorder: materials / uniforms",
                Milliseconds(profile.cpu_material_resolution_ms) + " / " +
                Milliseconds(profile.cpu_uniform_write_ms));
            Row(ui, "Descriptors: search / allocate / update",
                Milliseconds(profile.descriptor_search_cpu_ms) + " / " +
                Milliseconds(profile.descriptor_allocation_cpu_ms) + " / " +
                Milliseconds(profile.descriptor_update_cpu_ms));
            Row(ui, "Descriptor sets / pools", std::to_string(profile.descriptor_sets_created) +
                " / " + std::to_string(profile.descriptor_pools_created));
            Row(ui, "Pipeline validation", Milliseconds(profile.pipeline_validation_cpu_ms) +
                " (" + std::to_string(profile.pipeline_validation_calls) + " calls)");
            Row(ui, "Binding requests / emitted",
                "pipeline " + std::to_string(profile.pipeline_bind_requests) + "/" +
                    std::to_string(profile.pipeline_bind_emitted) + ", mesh " +
                    std::to_string(profile.mesh_bind_requests) + "/" +
                    std::to_string(profile.mesh_bind_emitted) + ", resource " +
                    std::to_string(profile.resource_binding_bind_requests) + "/" +
                    std::to_string(profile.resource_binding_bind_emitted));
        });

        ui.Section("Per-pass CPU / GPU");
        const UiTableColumn pass_columns[]{{"Pass", 145.0F}, {"CPU", 55.0F},
                                           {"GPU", 55.0F}, {"Draws / sections", 0.0F}};
        ui.Table("render.profiler.passes", pass_columns, 220.0F, true, [&]
        {
            for (const RenderProfilePass pass : kPasses)
            {
                const RenderProfilePassMetrics &sample =
                    profile.passes[static_cast<std::size_t>(pass)];
                ui.TableRow();
                ui.TableColumn(); ui.Text(PassName(pass));
                ui.TableColumn(); ui.Text(Milliseconds(sample.cpu_time_ms));
                ui.TableColumn(); ui.Text(Milliseconds(sample.gpu_time_ms));
                ui.TableColumn(); ui.Text(std::to_string(sample.draw_calls) + " / " +
                                           std::to_string(sample.sections));
            }
        });

        ui.Section("Ray tracing and memory");
        ui.Table("render.profiler.resources", summary_columns, 0.0F, false, [&]
        {
            Row(ui, "Path tracing", !profile.path_tracing_enabled ? "disabled"
                : profile.path_trace_active ? "active"
                : profile.path_tracing_available ? "available" : "unavailable");
            Row(ui, "Path trace samples / dispatch", std::to_string(profile.path_trace_samples) +
                " / " + std::to_string(profile.path_trace_samples_per_dispatch));
            Row(ui, "Ray-query shadows", profile.ray_query_shadows_active
                ? "active" : profile.ray_query_shadows_available ? "available" : "unavailable");
            Row(ui, "RT geometry / instances", std::to_string(profile.ray_tracing_geometry_records) +
                " / " + std::to_string(profile.ray_tracing_instance_records));
            Row(ui, "RT materials / lights", std::to_string(profile.ray_tracing_material_records) +
                " / " + std::to_string(profile.ray_tracing_light_records));
            Row(ui, "BLAS build / update", std::to_string(profile.ray_tracing_blas_builds) +
                " / " + std::to_string(profile.ray_tracing_blas_updates));
            Row(ui, "TLAS build / update", std::to_string(profile.ray_tracing_tlas_builds) +
                " / " + std::to_string(profile.ray_tracing_tlas_updates));
            Row(ui, "AS storage", Bytes(profile.ray_tracing_acceleration_structure_storage_bytes));
            Row(ui, "RT table packed / uploaded",
                std::to_string(profile.ray_tracing_scene_table_records_packed) + " / " +
                std::to_string(profile.ray_tracing_scene_table_records_uploaded));
            Row(ui, "RT address buffers / upload",
                std::to_string(profile.ray_tracing_address_table_buffers_created) + " / " +
                Bytes(profile.ray_tracing_address_table_upload_bytes));
            Row(ui, "RT descriptor sets / pools",
                std::to_string(profile.ray_tracing_descriptor_sets_created) + " / " +
                std::to_string(profile.ray_tracing_descriptor_pools_created));
            Row(ui, "RT table cache hits / misses",
                std::to_string(profile.ray_tracing_scene_record_cache_hits_total) + " / " +
                std::to_string(profile.ray_tracing_scene_record_cache_misses_total));
            Row(ui, "Texture resident / source / decoded",
                Bytes(profile.textures.resident_bytes) + " / " +
                Bytes(profile.textures.source_bytes) + " / " + Bytes(profile.textures.decoded_bytes));
            Row(ui, "Texture residency tracking", profile.textures.tracked_residency_complete
                ? "complete" : "incomplete (" +
                    std::to_string(profile.textures.tracked_residency_incomplete_count) + ")");
            Row(ui, "Shadow cache hits / misses", std::to_string(profile.shadow_cache_hits) +
                " / " + std::to_string(profile.shadow_cache_misses));
            Row(ui, "Point shadow cache hits / misses",
                std::to_string(profile.point_shadow_cache_hits) + " / " +
                std::to_string(profile.point_shadow_cache_misses));
            Row(ui, "Render graph / present mode", profile.render_graph_mode + " / " +
                profile.present_mode);
            Row(ui, "Screen-space AO", profile.screen_space_ao_effective ? "active"
                : profile.screen_space_ao_requested ? "requested, unavailable" : "disabled");
            Row(ui, "Height fog / temporal AA",
                std::string(profile.height_fog_effective ? "active" : "inactive") + " / " +
                    (profile.temporal_aa_effective ? "active" : "inactive"));
            Row(ui, "TAA history resets", std::to_string(profile.temporal_aa_reset_count) +
                " (" + profile.temporal_aa_reset_reason + ")");
            Row(ui, "GPU texture payload", Bytes(profile.r7_active_texture_payload_bytes));
        });

        const auto json = BuildJson(metrics, timings).dump(2);
        const auto text = BuildText(metrics, timings);
        if (ui.Button({.id = "render.profiler.copy_json", .label = "Copy profiler JSON"}).activated)
            ui.SetClipboardText(json);
        ui.SameLine();
        if (ui.Button({.id = "render.profiler.copy_text", .label = "Copy profiler text"}).activated)
            ui.SetClipboardText(text);
    }
}
