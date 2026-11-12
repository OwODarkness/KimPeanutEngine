#include "editor/ui/host/editor_ui_host.h"

#include <imgui.h>
#include <chrono>
#include <algorithm>
#include <type_traits>
#include <stdexcept>
#include <unordered_map>

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
        imgui_frame_open_ = true;
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        last_viewport_ = {{viewport->WorkSize.x, viewport->WorkSize.y},
                          {scale.x, scale.y}, {viewport->WorkPos.x, viewport->WorkPos.y}};
        (void)ui_session_->BeginFrame({
            .observe = observe || snapshot_store_ != nullptr,
            .viewport = last_viewport_});
    }

    bool EditorUiHost::RenderFrame(
        const std::function<void(ui::UiContext &)> &build,
        const std::function<void(IEditorImguiRenderer &)> &before_submit,
        const bool observe)
    {
        if (!IsInitialized()) return false;
        const auto started = std::chrono::steady_clock::now();
        try
        {
            BeginFrame(observe);
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
            if (imgui_frame_open_)
            {
                ImGui::EndFrame();
                imgui_frame_open_ = false;
            }
            if (ui_session_) ui_session_->AbortFrame();
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
        imgui_frame_open_ = false;
        const auto build_finished = std::chrono::steady_clock::now();
        if (before_submit) before_submit(*renderer_);
        if (bloom_shader_data_configured_)
            renderer_->SetBloomShaders(bloom_vertex_shader_.get(), bloom_fragment_shader_.get());
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
            if (snapshot_store_) PublishSnapshot();
        }
    }

    void EditorUiHost::PublishSnapshot() noexcept
    {
        try
        {
            const ui::UiSemanticView semantic = ui_session_->Query();
            const ui::UiFrameResult &frame = semantic.FrameResult();
            if (!frame.complete)
            {
                last_ui_observation_.clear();
                snapshot_store_->Clear(semantic.SessionId());
                return;
            }
            ui::UiSnapshotCapture capture{};
            capture.session = semantic.SessionId();
            capture.frame = frame.frame;
            capture.host = snapshot_host_;
            capture.mode = snapshot_mode_;
            capture.viewport = last_viewport_;
            capture.nodes = frame.observations;
            const ui::UiSemanticTree &tree = semantic.GetUITree();
            std::unordered_map<ui::UiNodeId, const ui::UiSemanticTreeNode *> tree_nodes;
            tree_nodes.reserve(tree.nodes.size());
            for (const ui::UiSemanticTreeNode &node : tree.nodes)
                tree_nodes.emplace(node.id, &node);
            for (ui::UiNodeObservation &node : capture.nodes)
            {
                if (node.geometry != ui::UiGeometryProvenance::Unavailable) continue;
                const auto found = tree_nodes.find(node.id);
                if (found == tree_nodes.end() ||
                    found->second->geometry != ui::UiGeometryProvenance::DerivedFromChildren)
                    continue;
                node.bounds = found->second->bounds;
                node.geometry = found->second->geometry;
                node.clipped_bounds = found->second->clipped_bounds;
                node.visible = found->second->visible;
                node.clipped = found->second->clipped;
            }
            capture.styles = frame.styles;
            capture.virtual_list_total = frame.virtual_list_total;
            capture.virtual_list_first = frame.virtual_list_first;
            capture.virtual_list_last = frame.virtual_list_last;
            capture.has_virtual_list = frame.has_virtual_list;
            constexpr std::size_t maximum_nodes = 4096;
            constexpr std::size_t maximum_text_bytes = 1024U * 1024U;
            constexpr std::size_t maximum_string_bytes = 4096;
            if (capture.nodes.size() > maximum_nodes)
            {
                capture.omitted_nodes = capture.nodes.size() - maximum_nodes;
                capture.nodes.resize(maximum_nodes);
                capture.partial_coverage = true;
                capture.coverage_reasons.emplace_back("node_limit");
            }
            std::size_t text_bytes = 0;
            const auto bound_text = [&](std::string &text)
            {
                const std::size_t allowed = std::min({text.size(), maximum_string_bytes,
                    maximum_text_bytes - std::min(text_bytes, maximum_text_bytes)});
                if (allowed < text.size())
                {
                    std::size_t valid = allowed;
                    while (valid > 0 && valid < text.size() &&
                           (static_cast<unsigned char>(text[valid]) & 0xC0U) == 0x80U)
                        --valid;
                    text.resize(valid);
                    capture.partial_coverage = true;
                    if (std::find(capture.coverage_reasons.begin(), capture.coverage_reasons.end(),
                                  "text_limit") == capture.coverage_reasons.end())
                        capture.coverage_reasons.emplace_back("text_limit");
                }
                text_bytes += text.size();
            };
            for (ui::UiNodeObservation &node : capture.nodes)
            {
                bound_text(node.label);
                bound_text(node.description);
                bound_text(node.disabled_reason);
                bound_text(node.unit);
                bound_text(node.domain_summary);
                for (auto &column : node.table_columns) bound_text(column);
                if (node.chart)
                {
                    bound_text(node.chart->x.label);
                    bound_text(node.chart->x.unit);
                    bound_text(node.chart->y.label);
                    bound_text(node.chart->y.unit);
                    bound_text(node.chart->empty_label);
                    for (auto &series : node.chart->series) bound_text(series.label);
                }
                if (node.value)
                    if (auto *value = std::get_if<std::string>(&*node.value)) bound_text(*value);
                for (ui::UiChoice &choice : node.choices)
                {
                    bound_text(choice.key);
                    bound_text(choice.label);
                    if (auto *value = std::get_if<std::string>(&choice.value)) bound_text(*value);
                }
            }
            if (snapshot_workspaces_) capture.workspaces = snapshot_workspaces_();
            snapshot_store_->Publish(std::move(capture));
        }
        catch (...)
        {
            if (snapshot_store_ && snapshot_session_ != 0)
                snapshot_store_->Clear(snapshot_session_);
        }
    }

    void EditorUiHost::SetSnapshotSource(
        ui::UiSnapshotStore &store, std::string host, std::string mode,
        std::function<std::vector<ui::UiWorkspaceSnapshot>()> workspaces)
    {
        if (ui_session_ == nullptr || !ui_session_->IsInitialized())
            throw std::logic_error("UI snapshot source requires an initialized UI session");
        snapshot_store_ = &store;
        snapshot_session_ = ui_session_->Context().SessionId();
        snapshot_store_->Clear(snapshot_session_);
        snapshot_host_ = std::move(host);
        snapshot_mode_ = std::move(mode);
        snapshot_workspaces_ = std::move(workspaces);
    }

    void EditorUiHost::BeginClosing()
    {
        if (ui_session_) ui_session_->BeginClosing();
        image_sources_.clear();
    }

    void EditorUiHost::Close()
    {
        if (snapshot_store_)
            snapshot_store_->Clear(snapshot_session_);
        snapshot_store_ = nullptr;
        snapshot_session_ = 0;
        snapshot_workspaces_ = {};
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

    void EditorUiHost::SetBloomShaderData(
        std::shared_ptr<data::ShaderData> vertex,
        std::shared_ptr<data::ShaderData> fragment)
    {
        bloom_vertex_shader_ = std::move(vertex);
        bloom_fragment_shader_ = std::move(fragment);
        bloom_shader_data_configured_ = true;
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
