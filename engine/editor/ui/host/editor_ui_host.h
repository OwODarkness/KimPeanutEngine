#ifndef KPENGINE_EDITOR_UI_HOST_H
#define KPENGINE_EDITOR_UI_HOST_H

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

#include "base/type.h"
#include "editor/ui/api/editor_ui/ui_session.h"
#include "editor/ui/api/editor_ui/ui_snapshot.h"
#include "editor/settings/editor_settings.h"
#include "graphics/backend/common/editor_presentation_bridge.h"
#include "graphics/backend/common/render_target.h"

struct ImFont;

namespace kpengine::data { struct ShaderData; }

namespace kpengine::editor
{
    class IEditorImguiRenderer;
    class IEditorImguiWSI;

    struct EditorUiHostInitInfo
    {
        WindowHandle window = nullptr;
        graphics::IEditorPresentationBridge *presentation_bridge = nullptr;
        std::function<std::unique_ptr<IEditorImguiRenderer>(GraphicsAPIType)> renderer_factory;
        std::function<std::unique_ptr<IEditorImguiWSI>()> wsi_factory;
        std::optional<LogColor> background_color;
    };

    class EditorUiHost final
    {
    public:
        EditorUiHost();
        ~EditorUiHost();
        EditorUiHost(const EditorUiHost &) = delete;
        EditorUiHost &operator=(const EditorUiHost &) = delete;

        void Initialize(const EditorUiHostInitInfo &info);
        bool RenderFrame(
            const std::function<void(ui::UiContext &)> &build,
            const std::function<void(IEditorImguiRenderer &)> &before_submit = {},
            bool observe = false);
        void BeginFrame(bool observe);
        std::pair<double, double> SubmitFrame(
            const std::function<void(IEditorImguiRenderer &)> &before_submit = {});
        void EndFrame();
        void SetSnapshotSource(ui::UiSnapshotStore &store, std::string host,
                               std::string mode,
                               std::function<std::vector<ui::UiWorkspaceSnapshot>()> workspaces);
        void BeginClosing();
        void Close();

        void RegisterRenderTargetImage(ui::UiImageId id,
                                       const graphics::RenderTargetView &view);
        void SetBloomShaderData(std::shared_ptr<data::ShaderData> vertex,
                                std::shared_ptr<data::ShaderData> fragment);
        void ClearFrameImages() noexcept { image_sources_.clear(); }
        void ClearLastObservation() noexcept { last_ui_observation_.clear(); }
        ui::UiContext &Context();
        IEditorImguiRenderer *Renderer() noexcept { return renderer_.get(); }
        ImFont *CodeFont() const noexcept { return code_font_; }
        double LastRenderTimeMs() const noexcept { return last_render_time_ms_; }
        double LastBuildTimeMs() const noexcept { return last_build_time_ms_; }
        double LastSubmitTimeMs() const noexcept { return last_submit_time_ms_; }
        bool IsInitialized() const noexcept { return imgui_context_created_ && renderer_ && wsi_; }
        ui::UiSessionId SessionId() const noexcept
        { return ui_session_ ? ui_session_->Context().SessionId() : 0; }
        const std::vector<ui::UiNodeObservation> &LastObservation() const noexcept
        {
            return last_ui_observation_;
        }

    private:
        void CreateBackends(const EditorUiHostInitInfo &info);
        void PublishSnapshot() noexcept;

        std::unique_ptr<IEditorImguiRenderer> renderer_;
        std::unique_ptr<IEditorImguiWSI> wsi_;
        std::shared_ptr<data::ShaderData> bloom_vertex_shader_;
        std::shared_ptr<data::ShaderData> bloom_fragment_shader_;
        bool bloom_shader_data_configured_ = false;
        std::unique_ptr<ui::UiSession> ui_session_;
        std::unordered_map<ui::UiImageId, graphics::RenderTargetView> image_sources_;
        std::vector<ui::UiNodeObservation> last_ui_observation_;
        ui::UiSnapshotStore *snapshot_store_ = nullptr;
        ui::UiSessionId snapshot_session_ = 0;
        std::string snapshot_host_;
        std::string snapshot_mode_;
        std::function<std::vector<ui::UiWorkspaceSnapshot>()> snapshot_workspaces_;
        ui::UiViewportInfo last_viewport_{};
        ImFont *code_font_ = nullptr;
        bool imgui_context_created_ = false;
        bool imgui_frame_open_ = false;
        bool renderer_init_attempted_ = false;
        bool wsi_init_attempted_ = false;
        double last_render_time_ms_ = 0.0;
        double last_build_time_ms_ = 0.0;
        double last_submit_time_ms_ = 0.0;
    };
}

#endif
