#include "live2d_viewer_editor.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "editor/ui/api/editor_ui/ui_backend.h"
#include "editor/ui/api/editor_ui/ui_theme.h"
#include "runtime/core/log/editor/log_editor_view.h"

namespace kpengine::live2d::editor
{
    namespace
    {
        const kpengine::editor::ui::UiImageId kViewerImageId =
            kpengine::editor::ui::StableUiImageId("live2d.viewer.output");

        std::string Number(const double value, const char *format = "%.2f")
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), format, value);
            return buffer;
        }

        std::string Hex(const std::uint32_t value)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
            return buffer;
        }

        void Field(kpengine::editor::ui::UiContext &ui, const std::string &label,
                   const std::string &value)
        {
            ui.Text(label + "  " + value, kpengine::editor::ui::UiTextRole::Muted);
        }

        kpengine::editor::ui::UiTheme Live2DTheme()
        {
            kpengine::editor::ui::UiTheme theme{};
            theme.surface = {0.055F, 0.065F, 0.085F, 1.0F};
            theme.panel = {0.075F, 0.090F, 0.115F, 1.0F};
            theme.raised = {0.11F, 0.13F, 0.17F, 1.0F};
            theme.border = {0.24F, 0.29F, 0.37F, 1.0F};
            theme.text = {0.91F, 0.93F, 0.97F, 1.0F};
            theme.muted = {0.62F, 0.67F, 0.76F, 1.0F};
            theme.accent = {0.47F, 0.72F, 0.98F, 1.0F};
            theme.accent_active = {0.97F, 0.68F, 0.32F, 1.0F};
            theme.selected = {0.16F, 0.25F, 0.37F, 1.0F};
            theme.selected_hovered = {0.22F, 0.34F, 0.49F, 1.0F};
            return theme;
        }
    }

    struct Live2DViewerEditor::Impl final
    {
        std::unique_ptr<kpengine::editor::EditorUiHost> ui;
        std::unique_ptr<kpengine::editor::LogEditorView> log;
        kpengine::LogSystem *log_system = nullptr;
        Live2DViewerEditorState state{};
        Live2DViewerEditorActions actions{};

        void RenderViewer(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            const UiViewportInfo &viewport = context.Viewport();
            const UiVec2 viewer_size{viewport.logical_extent.x * 0.70F,
                                     viewport.logical_extent.y * 0.75F};
            const auto window = context.Window({.id = "live2d.viewer.window", .label = "Live2D Viewer"},
                viewport.logical_origin, viewer_size);
            if (!window.ContentVisible()) return;

            UiVec2 available = context.AvailableContent();
            available.x = std::max(available.x, 1.0F);
            available.y = std::max(available.y, 1.0F);
            const float aspect = state.output_view.height > 0
                ? static_cast<float>(state.output_view.width) /
                    static_cast<float>(state.output_view.height)
                : 720.0F / 960.0F;
            const float image_height = std::min(available.y, available.x / std::max(aspect, 0.01F));
            const float image_width = image_height * aspect;
            const UiVec2 image_min{(available.x - image_width) * 0.5F / available.x,
                                   (available.y - image_height) * 0.5F / available.y};
            const UiVec2 image_max{image_min.x + image_width / available.x,
                                   image_min.y + image_height / available.y};

            std::vector<UiCanvasPrimitive> primitives;
            UiCanvasPrimitive background{};
            background.kind = UiCanvasPrimitiveKind::FilledRect;
            background.first = {0.0F, 0.0F};
            background.second = {1.0F, 1.0F};
            background.color = {0.025F, 0.031F, 0.043F, 1.0F};
            primitives.push_back(std::move(background));
            if (state.output_view.IsValid())
            {
                UiCanvasPrimitive image{};
                image.kind = UiCanvasPrimitiveKind::Image;
                image.first = image_min;
                image.second = image_max;
                image.image = kViewerImageId;
                primitives.push_back(std::move(image));
            }
            else
            {
                UiCanvasPrimitive message{};
                message.kind = UiCanvasPrimitiveKind::Text;
                message.first = {0.5F, 0.5F};
                message.color = {0.62F, 0.67F, 0.76F, 1.0F};
                message.text = "Live2D render target unavailable";
                message.text_anchor = {0.5F, 0.5F};
                primitives.push_back(std::move(message));
            }
            const UiCanvasResult result = context.Canvas(
                {.id = "live2d.viewer.canvas", .label = "Live2D model"},
                available, std::move(primitives));
            if (result.geometry == UiGeometryProvenance::ActualBackend &&
                actions.report_image_rect && state.output_view.IsValid())
            {
                const UiVec2 canvas_extent{result.bounds.max.x - result.bounds.min.x,
                                           result.bounds.max.y - result.bounds.min.y};
                const UiVec2 screen_min{result.bounds.min.x + image_min.x * canvas_extent.x,
                                        result.bounds.min.y + image_min.y * canvas_extent.y};
                const UiVec2 screen_size{(image_max.x - image_min.x) * canvas_extent.x,
                                         (image_max.y - image_min.y) * canvas_extent.y};
                actions.report_image_rect(Vector2f{screen_min.x, screen_min.y},
                                          Vector2f{screen_size.x, screen_size.y});
            }
        }

        void RenderGaze(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            context.Section("GAZE TARGETING");
            context.Text("The runtime smooths the target before applying it.", UiTextRole::Muted);
            const auto mode = context.Combo<std::int64_t>(
                {.id = "live2d.gaze.mode", .label = "Mode"},
                static_cast<std::int64_t>(state.gaze_mode), {
                    {"neutral", "Neutral", static_cast<std::int64_t>(Live2DGazeMode::Neutral)},
                    {"mouse", "Follow Mouse", static_cast<std::int64_t>(Live2DGazeMode::FollowMouse)},
                    {"fixed", "Fixed Target", static_cast<std::int64_t>(Live2DGazeMode::FixedTarget)}});
            if (mode.changed && actions.set_gaze)
            {
                actions.set_gaze(static_cast<Live2DGazeMode>(mode.value), state.fixed_gaze_target);
            }
            if (state.gaze_mode == Live2DGazeMode::FixedTarget)
            {
                const auto x = context.Slider<double>(
                    {.id = "live2d.gaze.fixed.x", .label = "Fixed target X"},
                    state.fixed_gaze_target[0], -1.0, 1.0);
                const auto y = context.Slider<double>(
                    {.id = "live2d.gaze.fixed.y", .label = "Fixed target Y"},
                    state.fixed_gaze_target[1], -1.0, 1.0);
                if ((x.changed || y.changed) && actions.set_gaze)
                {
                    actions.set_gaze(state.gaze_mode,
                        Vector2f{static_cast<float>(x.value), static_cast<float>(y.value)});
                }
            }
            context.Section("CURRENT TARGET");
            Field(context, "X", Number(state.last_gaze_target[0], "%+.3f"));
            Field(context, "Y", Number(state.last_gaze_target[1], "%+.3f"));
        }

        void RenderPlayback(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            context.Section("PLAYBACK CONTROL");
            context.Text("Freeze authored motion while inspecting a pose.", UiTextRole::Muted);
            if (context.Button({.id = "live2d.playback.toggle",
                                .label = state.paused ? "RESUME" : "PAUSE"}).activated &&
                actions.set_paused)
            {
                actions.set_paused(!state.paused);
            }
            if (context.Button({.id = "live2d.playback.step", .label = "STEP ONE FRAME"}).activated)
            {
                if (actions.set_paused) actions.set_paused(true);
                if (actions.request_step) actions.request_step();
            }
            if (context.Button({.id = "live2d.playback.reset", .label = "RESET PARAMETERS"}).activated &&
                actions.request_reset_parameters)
            {
                actions.request_reset_parameters();
            }
            Field(context, "State", state.paused ? "PAUSED" : "PLAYING");
        }

        void RenderTelemetry(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            context.Section("RUNTIME TELEMETRY");
            if (!state.renderer_ready)
            {
                context.Text("Live2D runtime is not ready", UiTextRole::Warning);
                return;
            }
            Field(context, "Update sequence", std::to_string(state.update_sequence));
            Field(context, "Snapshot sequence", std::to_string(state.frame_sequence));
            Field(context, "Parameters", std::to_string(state.parameter_count));
            Field(context, "Drawables", std::to_string(state.features.drawable_count));
            context.Section("ACTIVE BEHAVIORS");
            constexpr std::array<std::string_view, 5> labels{
                "Blink", "Gaze", "Breath", "Physics", "Pose"};
            constexpr std::array<std::uint32_t, 5> bits{
                kLive2DBehaviorBlink, kLive2DBehaviorGaze, kLive2DBehaviorBreath,
                kLive2DBehaviorPhysics, kLive2DBehaviorPose};
            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                const bool active = (state.behavior_mask & bits[index]) != 0U;
                Field(context, std::string(labels[index]), active ? "ON" : "OFF");
            }
            context.Section("CAPABILITIES");
            Field(context, "Hit areas", state.capabilities.has_hit_areas ? "available" : "none");
            Field(context, "User data", state.capabilities.has_user_data ? "available" : "none");
            Field(context, "Secondary", state.capabilities.has_secondary_behavior
                ? "available" : "reimport required");
            Field(context, "Behavior mask", Hex(state.behavior_mask));
        }

        void RenderEmotion(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            context.Section("EMOTION");
            context.Text("Choose a face and body-language preset.", UiTextRole::Muted);
            constexpr std::array<std::string_view, 4> presets{"Normal", "Sad", "Angry", "Happy"};
            for (std::size_t index = 0; index < presets.size(); ++index)
            {
                const std::string id = "live2d.emotion." + std::string(presets[index]);
                const std::string label(presets[index]);
                if (context.Button({.id = id, .label = label}).activated && actions.apply_emotion)
                    actions.apply_emotion(presets[index]);
            }
            Field(context, "Current", state.emotion_status);
            if (!state.emotion_diagnostic.empty())
                context.WrappedText(state.emotion_diagnostic);
        }

        void RenderControlPanel(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            const UiViewportInfo &viewport = context.Viewport();
            const UiVec2 position{viewport.logical_origin.x + viewport.logical_extent.x * 0.70F,
                                  viewport.logical_origin.y};
            const UiVec2 size{viewport.logical_extent.x * 0.30F,
                              viewport.logical_extent.y * 0.43F};
            const auto window = context.Window(
                {.id = "live2d.controls.window", .label = "Live2D Control Deck"}, position, size);
            if (!window.ContentVisible()) return;
            context.Text("RUNTIME DEBUG", UiTextRole::Muted);
            const auto tabs = context.TabBar({.id = "live2d.controls.tabs", .label = "Live2D controls"});
            if (!tabs.Visible()) return;
            {
                const auto tab = context.TabItem({.id = "live2d.controls.gaze", .label = "Gaze"});
                if (tab.ContentVisible()) RenderGaze(context);
            }
            {
                const auto tab = context.TabItem({.id = "live2d.controls.playback", .label = "Playback"});
                if (tab.ContentVisible()) RenderPlayback(context);
            }
            {
                const auto tab = context.TabItem({.id = "live2d.controls.telemetry", .label = "Telemetry"});
                if (tab.ContentVisible()) RenderTelemetry(context);
            }
            {
                const auto tab = context.TabItem({.id = "live2d.controls.emotion", .label = "Emotion"});
                if (tab.ContentVisible()) RenderEmotion(context);
            }
        }

        void RenderProfilerWindow(kpengine::editor::ui::UiContext &context)
        {
            using namespace kpengine::editor::ui;
            const UiViewportInfo &viewport = context.Viewport();
            const UiVec2 position{viewport.logical_origin.x + viewport.logical_extent.x * 0.70F,
                                  viewport.logical_origin.y + viewport.logical_extent.y * 0.44F};
            const UiVec2 size{viewport.logical_extent.x * 0.30F,
                              viewport.logical_extent.y * 0.31F};
            const auto window = context.Window(
                {.id = "live2d.profiler.window", .label = "Performance Profiler"}, position, size);
            if (!window.ContentVisible()) return;
            context.Text("GPU submission and frame timing", UiTextRole::Muted);
            Field(context, "API", state.graphics_api == GraphicsAPIType::GRAPHICS_API_VULKAN
                ? "Vulkan" : "OpenGL");
            Field(context, "Frame", Number(state.frame_total_ms) + " ms");
            Field(context, "Render work", Number(state.render_work_ms) + " ms");
            Field(context, "Editor UI work", Number(state.ui_work_ms) + " ms");
            Field(context, "Tick work", Number(state.game_tick_work_ms) + " ms");
            context.Section("LIVE2D / BACKEND");
            Field(context, "Draw calls", std::to_string(state.recorder_counters.draw_calls_emitted));
            Field(context, "Pipeline binds", std::to_string(state.recorder_counters.pipeline_bind_emitted));
            Field(context, "Resource binds", std::to_string(state.recorder_counters.resource_binding_bind_emitted));
            Field(context, "Descriptor updates", std::to_string(state.backend_counters.descriptor_updates));
            Field(context, "Module GPU handles", std::to_string(state.live_gpu_handles));
            Field(context, "Frame sequence", std::to_string(state.frame_sequence));
            Field(context, "UI render", Number(ui->LastRenderTimeMs()) + " ms");
        }

        void RenderLog(kpengine::editor::ui::UiContext &context)
        {
            if (!log) return;
            const auto &viewport = context.Viewport();
            const kpengine::editor::ui::UiVec2 position{
                viewport.logical_origin.x,
                viewport.logical_origin.y + viewport.logical_extent.y * 0.75F};
            const kpengine::editor::ui::UiVec2 size{
                viewport.logical_extent.x, viewport.logical_extent.y * 0.25F};
            const auto window = context.Window(
                {.id = "live2d.log.window", .label = "Output Log"}, position, size);
            if (window.ContentVisible()) log->RenderContent(context, log_system);
        }

        void Render(kpengine::editor::ui::UiContext &context)
        {
            [[maybe_unused]] const auto theme = context.Theme(Live2DTheme());
            RenderViewer(context);
            RenderLog(context);
            RenderControlPanel(context);
            RenderProfilerWindow(context);
        }
    };

    Live2DViewerEditor::Live2DViewerEditor() : impl_(std::make_unique<Impl>()) {}
    Live2DViewerEditor::~Live2DViewerEditor() = default;

    bool Live2DViewerEditor::Initialize(
        const kpengine::editor::EditorUiHostInitInfo &init_info,
        kpengine::LogSystem *log_system,
        const kpengine::editor::LogLevelColorTable &log_colors,
        std::string &diagnostic)
    {
        diagnostic.clear();
        impl_->log_system = log_system;
        kpengine::editor::LogEditorColorTable colors{};
        for (std::size_t index = 0; index < colors.size(); ++index)
        {
            colors[index] = {log_colors[index].r, log_colors[index].g,
                             log_colors[index].b, log_colors[index].a};
        }
        impl_->log = std::make_unique<kpengine::editor::LogEditorView>(colors);
        impl_->ui = std::make_unique<kpengine::editor::EditorUiHost>();
        impl_->ui->Initialize(init_info);
        return true;
    }

    bool Live2DViewerEditor::Render(const Live2DViewerEditorState &state,
                                    Live2DViewerEditorActions actions,
                                    std::string &diagnostic)
    {
        diagnostic.clear();
        if (impl_->ui == nullptr)
        {
            diagnostic = "Live2D editor UI is not initialized";
            return false;
        }
        impl_->state = state;
        impl_->actions = std::move(actions);
        impl_->ui->RegisterRenderTargetImage(kViewerImageId, state.output_view);
        if (!impl_->ui->RenderFrame(
                [this](kpengine::editor::ui::UiContext &ui) { impl_->Render(ui); }))
        {
            diagnostic = "Live2D viewer UI presentation failed";
            return false;
        }
        return true;
    }

    void Live2DViewerEditor::Close() noexcept
    {
        if (impl_ && impl_->ui)
        {
            impl_->ui->Close();
        }
        if (impl_)
        {
            impl_->ui.reset();
            impl_->log.reset();
        }
    }

    double Live2DViewerEditor::GetLastRenderTimeMs() const noexcept
    {
        return impl_->ui ? impl_->ui->LastRenderTimeMs() : 0.0;
    }
}
