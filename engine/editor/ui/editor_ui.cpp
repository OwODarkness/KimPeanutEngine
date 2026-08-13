#include "editor/ui/editor_ui.h"

#include <imgui.h>
#include "config/path.h"
#include "editor/platform/editor_imgui_glfw_wsi.h"
#include "editor/platform/editor_imgui_opengl_renderer.h"
#include "editor/platform/editor_imgui_vulkan_renderer.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/component/editor_text_component.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "log/logger.h"

namespace kpengine::editor
{
    EditorUI::EditorUI() = default;

    void EditorUI::Initialize(WindowHandle window, GraphicsAPIType backend_type, LogSystem* log_system)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        // WSI + renderer are chosen by the active graphics API, keeping the editor
        // UI decoupled from GL/Vulkan (the reconstruction seam).
        wsi_ = std::make_unique<EditorImguiGLFWWSI>();
        if (backend_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            renderer_ = std::make_unique<EditorImguiOpenglRenderer>();
        }
        else if (backend_type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            renderer_ = std::make_unique<EditorImguiVulkanRenderer>();
        }

        GraphicsContext context{backend_type, nullptr};
        renderer_->Initialize(context);
        wsi_->Initialize(window, backend_type);

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Log window colors come from config/settings.json. Cosmetic, not a boot
        // error: a missing/malformed file logs a warning and keeps the defaults.
        LogLevelColorTable log_colors = DefaultLogColors();
        try
        {
            log_colors = ReadEditorSettings(GetSettingsPath()).log_colors;
        }
        catch (const std::exception &e)
        {
            KP_LOG("LogEditorUI", LOG_LEVEL_WARNING,
                   "editor settings unavailable (%s), using default log colors", e.what());
        }

        // Minimal editor UI: a label window plus the log window, which re-reads the
        // runtime log system each frame (ImGui immediate mode).
        std::unique_ptr<EditorWindowComponent> window_component = std::make_unique<EditorWindowComponent>("window");
        window_component->AddComponent(std::make_shared<EditorTextComponent>("hello imgui"));
        components_.push_back(std::move(window_component));
        components_.push_back(std::make_unique<EditorLogComponent>(log_system, log_colors));
    }

    void EditorUI::Close()
    {
        renderer_->Shutdown();
        wsi_->Shutdown();
        ImGui::DestroyContext();
        renderer_.reset();
        wsi_.reset();
        components_.clear();
    }

    void EditorUI::BeginDraw()
    {
        renderer_->NewFrame();
        wsi_->NewFrame();
        ImGui::NewFrame();
    }

    void EditorUI::EndDraw()
    {
    }

    bool EditorUI::Render()
    {
        for (const auto &component : components_)
        {
            component->Render();
        }
        ImGui::Render();
        renderer_->Render();

        return true;
    }

    EditorUI::~EditorUI() = default;

}