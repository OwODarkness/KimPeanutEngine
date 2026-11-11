#include "editor/ui/host/editor_ui_host.h"

#include <imgui.h>
#include <chrono>
#include <stdexcept>

#include "editor/platform/editor_imgui_glfw_wsi.h"
#include "editor/platform/editor_imgui_opengl_renderer.h"
#include "editor/platform/editor_imgui_renderer.h"
#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include "editor/platform/editor_imgui_wsi.h"
#include "editor/ui/api/ui_imgui_inline_backend_internal.h"
#include "editor/ui/editor_theme.h"
#include "editor/settings/editor_settings.h"
#include "config/path.h"

namespace kpengine::editor
{
    EditorUiHost::EditorUiHost() = default;

    EditorUiHost::~EditorUiHost()
    {
        Close();
    }

    void EditorUiHost::Initialize(const EditorUiHostInitInfo &info)
    {
        if (IsInitialized() || ui_session_ || imgui_context_created_)
            Close();
        try
        {
            if (info.presentation_bridge == nullptr)
                throw std::runtime_error("Editor presentation bridge is unavailable");
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            imgui_context_created_ = true;
            code_font_ = ApplyCodexTheme();
            CreateBackends(info);
            LogColor background = info.background_color.value_or(EditorSettings{}.background_color);
            if (!info.background_color)
            {
                try { background = ReadEditorSettings(GetSettingsPath()).background_color; }
                catch (...) {}
            }
            renderer_->SetBackgroundColor(background);

            ui_session_ = std::make_unique<ui::UiSession>(ui::CreateImGuiInlineBackend(
                code_font_, [this](const ui::UiImageId id, const ui::UiRect bounds,
                                   ImDrawList *const draw_list)
                {
                    const auto source = image_sources_.find(id);
                    if (source == image_sources_.end() || renderer_ == nullptr ||
                        !source->second.IsValid())
                        return;
                    renderer_->DrawSceneImagePrimitive(draw_list,
                        renderer_->GetTextureID(source->second),
                        {bounds.min.x, bounds.min.y}, {bounds.max.x, bounds.max.y});
                }));
            if (!ui_session_->Initialize({}))
                throw std::runtime_error("Editor UI widget backend could not attach to ImGui");

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            io.ConfigDragClickToInputText = true;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    void EditorUiHost::CreateBackends(const EditorUiHostInitInfo &info)
    {
        wsi_ = info.wsi_factory ? info.wsi_factory()
                                : std::make_unique<EditorImguiGLFWWSI>();
        if (!wsi_)
            throw std::runtime_error("Editor ImGui window backend is unavailable");

        if (info.renderer_factory)
            renderer_ = info.renderer_factory(info.presentation_bridge->GetGraphicsAPI());
        else if (info.presentation_bridge->GetGraphicsAPI() ==
                 GraphicsAPIType::GRAPHICS_API_OPENGL)
            renderer_ = std::make_unique<EditorImguiOpenglRenderer>();
        else if (info.presentation_bridge->GetGraphicsAPI() ==
                 GraphicsAPIType::GRAPHICS_API_VULKAN)
            renderer_ = std::make_unique<EditorImguiVulkanRenderer>();
        if (!renderer_)
            throw std::runtime_error("Unsupported graphics API for Editor UI");

        renderer_init_attempted_ = true;
        if (!renderer_->Initialize(info.presentation_bridge))
            throw std::runtime_error("Editor ImGui renderer initialization failed");
        wsi_init_attempted_ = true;
        if (!wsi_->Initialize(info.window, info.presentation_bridge->GetGraphicsAPI()))
            throw std::runtime_error("Editor ImGui window backend initialization failed");
    }

    void EditorUiHost::BeginFrame(const bool observe)
    {
        if (!IsInitialized() || !ui_session_)
            throw std::logic_error("Editor UI host is not initialized");
        renderer_->NewFrame();
        wsi_->NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        (void)ui_session_->BeginFrame({
            .observe = observe,
            .viewport = {{viewport->WorkSize.x, viewport->WorkSize.y},
                         {scale.x, scale.y}, {viewport->WorkPos.x, viewport->WorkPos.y}}});
    }

    bool EditorUiHost::RenderFrame(
        const std::function<void(ui::UiContext &)> &build,
        const std::function<void(IEditorImguiRenderer &)> &before_submit,
        const bool observe)
    {
        if (!IsInitialized()) return false;
        const auto started = std::chrono::steady_clock::now();
        BeginFrame(observe);
        try
        {
            if (build) build(Context());
            const double submit_ms = SubmitFrame(before_submit).second;
            EndFrame();
            ClearFrameImages();
            const auto finished = std::chrono::steady_clock::now();
            last_build_time_ms_ =
                std::chrono::duration<double, std::milli>(finished - started).count() - submit_ms;
            last_submit_time_ms_ = submit_ms;
            last_render_time_ms_ = last_build_time_ms_ + last_submit_time_ms_;
            return true;
        }
        catch (...)
        {
            ImGui::EndFrame();
            EndFrame();
            ClearFrameImages();
            throw;
        }
    }

    std::pair<double, double> EditorUiHost::SubmitFrame(
        const std::function<void(IEditorImguiRenderer &)> &before_submit)
    {
        if (!IsInitialized())
            throw std::logic_error("Editor UI host is not initialized");
        const auto build_started = std::chrono::steady_clock::now();
        ImGui::Render();
        const auto build_finished = std::chrono::steady_clock::now();
        if (before_submit) before_submit(*renderer_);
        renderer_->Render();
        const auto submit_finished = std::chrono::steady_clock::now();
        return {
            std::chrono::duration<double, std::milli>(build_finished - build_started).count(),
            std::chrono::duration<double, std::milli>(submit_finished - build_finished).count()};
    }

    void EditorUiHost::EndFrame()
    {
        if (ui_session_ && ui_session_->State() == ui::UiSessionState::InFrame)
        {
            const ui::UiFrameResult frame = ui_session_->EndFrame();
            if (frame.complete) last_ui_observation_ = frame.observations;
        }
    }

    void EditorUiHost::BeginClosing()
    {
        if (ui_session_) ui_session_->BeginClosing();
        image_sources_.clear();
    }

    void EditorUiHost::Close()
    {
        if (ui_session_)
        {
            ui_session_->Shutdown();
            ui_session_.reset();
        }
        image_sources_.clear();
        last_ui_observation_.clear();
        if (wsi_ && wsi_init_attempted_) wsi_->Shutdown();
        wsi_init_attempted_ = false;
        if (renderer_ && renderer_init_attempted_) renderer_->Shutdown();
        renderer_init_attempted_ = false;
        if (imgui_context_created_)
        {
            ImGui::DestroyContext();
            imgui_context_created_ = false;
        }
        renderer_.reset();
        wsi_.reset();
        code_font_ = nullptr;
        last_render_time_ms_ = 0.0;
        last_build_time_ms_ = 0.0;
        last_submit_time_ms_ = 0.0;
    }

    void EditorUiHost::RegisterRenderTargetImage(
        const ui::UiImageId id, const graphics::RenderTargetView &view)
    {
        if (id == 0)
            throw std::invalid_argument("editor UI image source requires a non-zero ID");
        if (view.IsValid()) image_sources_.insert_or_assign(id, view);
        else image_sources_.erase(id);
    }

    ui::UiContext &EditorUiHost::Context()
    {
        if (!ui_session_) throw std::logic_error("Editor UI host has no UI session");
        return ui_session_->Context();
    }
}
