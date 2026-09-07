#include "editor/ui/component/editor_gpu_profiler_component.h"

#include <cstdio>
#include <initializer_list>
#include <optional>
#include <sstream>

#include "imgui.h"
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
            const double imgui_submit_ms)
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
                   << "  GPU total: " << FormatMilliseconds(total) << "\n"
                   << "  Shadow cache: hits " << profile.shadow_cache_hits
                   << ", misses " << profile.shadow_cache_misses << "\n"
                   << "  Draw calls: " << profile.draw_calls << "\n"
                   << "  Triangles: " << triangle_count << "\n"
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
            append_pass("Tone map", RenderProfilePass::ToneMap);
            append_pass("Capture view", RenderProfilePass::CaptureView);
            append_pass("ImGui composite", RenderProfilePass::EditorComposite);
            return copied.str();
        }
    }

    EditorGpuProfilerComponent::EditorGpuProfilerComponent(runtime::Engine *engine,
                                                           render::RenderSystem *render_system,
                                                           const EditorUI *editor_ui)
        : EditorWindowComponent("Performance Profiler", EditorWindowConfig{0.8f, 0.70f, 0.2f,
                                                                              0.34f, true}),
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
            SumPasses(profile, {RenderProfilePass::ToneMap, RenderProfilePass::CaptureView});
        const std::optional<double> imgui_gpu =
            SumPasses(profile, {RenderProfilePass::EditorComposite});
        const std::optional<double> total = SumPasses(
            profile, {RenderProfilePass::DirectionalShadow, RenderProfilePass::SpotShadow,
                      RenderProfilePass::PointShadow, RenderProfilePass::GBuffer,
                      RenderProfilePass::DeferredLighting, RenderProfilePass::ToneMap,
                      RenderProfilePass::CaptureView, RenderProfilePass::EditorComposite});

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
            ImGui::EndTable();
        }

        const runtime::Engine::FrameLoopMetrics frame =
            engine_ != nullptr ? engine_->GetFrameLoopMetrics()
                               : runtime::Engine::FrameLoopMetrics{};
        const double imgui_total_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastRenderTimeMs() : 0.0;
        const double imgui_build_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastImGuiBuildTimeMs() : 0.0;
        const double imgui_submit_ms =
            editor_ui_ != nullptr ? editor_ui_->GetLastImGuiSubmitTimeMs() : 0.0;

        if (ImGui::Button("Copy profiler data"))
        {
            const std::string copied = BuildClipboardText(
                profile, shadow, lighting, post_process, imgui_gpu, total,
                metrics.triangle_count, frame, imgui_total_ms, imgui_build_ms, imgui_submit_ms);
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
            DrawCpuPassRow("Tone map", passes[static_cast<size_t>(RenderProfilePass::ToneMap)]);
            DrawCpuPassRow("Capture view",
                           passes[static_cast<size_t>(RenderProfilePass::CaptureView)]);
            DrawCpuPassRow("ImGui composite",
                           passes[static_cast<size_t>(RenderProfilePass::EditorComposite)]);
            ImGui::EndTable();
        }

    }
}
