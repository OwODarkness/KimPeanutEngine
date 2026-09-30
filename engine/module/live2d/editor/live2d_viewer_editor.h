#ifndef KPENGINE_LIVE2D_VIEWER_EDITOR_H
#define KPENGINE_LIVE2D_VIEWER_EDITOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "editor/settings/editor_settings.h"
#include "editor/ui/editor_ui.h"
#include "graphics/backend/common/render_backend.h"
#include "math/math_header.h"
#include "module/live2d/render/live2d_render_contract.h"
#include "module/live2d/runtime/live2d_model_instance.h"
#include "module/live2d/runtime/live2d_model_resource.h"

namespace kpengine::live2d::editor
{
    enum class Live2DGazeMode : std::uint8_t
    {
        Neutral,
        FollowMouse,
        FixedTarget
    };

    struct Live2DViewerEditorState final
    {
        graphics::RenderTargetView output_view{};
        graphics::BackendProfileCounters backend_counters{};
        graphics::CommandRecorderProfileCounters recorder_counters{};
        GraphicsAPIType graphics_api = GraphicsAPIType::GRAPHICS_API_OPENGL;
        Live2DBehaviorCapabilities capabilities{};
        Live2DRenderFeatureReport features{};
        std::uint32_t behavior_mask = 0u;
        std::uint64_t update_sequence = 0u;
        std::uint64_t frame_sequence = 0u;
        std::uint64_t parameter_count = 0u;
        std::uint32_t live_gpu_handles = 0u;
        double frame_total_ms = 0.0;
        double render_work_ms = 0.0;
        double imgui_work_ms = 0.0;
        double game_tick_work_ms = 0.0;
        bool renderer_ready = false;
        Live2DGazeMode gaze_mode = Live2DGazeMode::Neutral;
        kpengine::Vector2f fixed_gaze_target{};
        kpengine::Vector2f last_gaze_target{};
        bool paused = false;
        std::string emotion_status = "Normal";
        std::string emotion_diagnostic;
    };

    struct Live2DViewerEditorActions final
    {
        std::function<void(Live2DGazeMode, kpengine::Vector2f)> set_gaze;
        std::function<void(bool)> set_paused;
        std::function<void()> request_step;
        std::function<void()> request_reset_parameters;
        std::function<void(std::string_view)> apply_emotion;
        std::function<void(kpengine::Vector2f, kpengine::Vector2f)> report_image_rect;
    };

    class Live2DViewerEditor final
    {
    public:
        Live2DViewerEditor();
        ~Live2DViewerEditor();

        Live2DViewerEditor(const Live2DViewerEditor &) = delete;
        Live2DViewerEditor &operator=(const Live2DViewerEditor &) = delete;

        bool Initialize(const kpengine::editor::EditorUIInitInfo &init_info,
                        const kpengine::editor::LogLevelColorTable &log_colors,
                        std::string &diagnostic);
        bool Render(const Live2DViewerEditorState &state,
                    Live2DViewerEditorActions actions,
                    std::string &diagnostic);
        void Close() noexcept;

        double GetLastImGuiBuildTimeMs() const noexcept;
        double GetLastImGuiSubmitTimeMs() const noexcept;
        double GetLastRenderTimeMs() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_EDITOR_H
