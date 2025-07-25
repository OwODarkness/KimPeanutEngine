#include "editor_ui.h"

#include <string>
#include <iostream>
#include <cstdio>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>


#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_tooltip_component.h"
#include "editor/include/editor_ui_component/editor_button_component.h"
#include "editor/include/editor_ui_component/editor_menubar_component.h"
#include "editor/include/editor_ui_component/editor_window_component.h"
#include "editor/include/editor_ui_component/editor_plot_component.h"
#include "editor/include/editor_ui_component/editor_listbox_component.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor_ui_component/editor_container_component.h"
#include "editor/include/editor_ui_component/editor_camera_component.h"
#include "editor/include/editor_actor_control_panel.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/level_system.h"
#include "runtime/engine.h"


namespace kpengine
{
    namespace ui
    {
        EditorUI::EditorUI()
        {
        }

        void EditorUI::Initialize(GLFWwindow *window)
        {

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();
            // Setup Dear ImGui style
            ImGui_ImplGlfw_InitForOpenGL(window, true);

            const char *glsl_version = "#version 460";
            ImGui_ImplOpenGL3_Init(glsl_version);

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
            //ImFont *font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\simhei.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
            //IM_ASSERT(font != nullptr);

            std::unique_ptr<EditorWindowComponent> window_component_ = std::make_unique<EditorWindowComponent>("window");
            window_component_->AddComponent(std::make_shared<EditorTextComponent>("hello imgui"));
            window_component_->AddComponent(std::make_shared<EditorTooltipComponent>("welcome"));

            std::shared_ptr<EditorContainerComponent> container = std::make_shared<EditorContainerComponent>();
           
            const int* fps = editor::global_editor_context.runtime_engine_->GetFPSRef();
            container->AddComponent(std::make_shared<EditorDynamicTextComponent<int>>(fps, "fps: "));

            const int* triangle_count = editor::global_editor_context.render_system_->GetTriangleCountRef();
            container->AddComponent(std::make_shared<EditorDynamicTextComponent<int>>(triangle_count, "trangle count this frame: "));
            window_component_->AddComponent(container);

            std::vector<const char*> items = {"custom", SHADER_CATEGORY_NORMAL};

            std::shared_ptr<EditorUIComponent> listbox = std::make_shared<EditorListboxComponent>(items);
            window_component_->AddComponent(listbox);

            components_.push_back(std::move(window_component_));

            //menu init
            std::vector<Menu> menus;
            menus.push_back({"File", {{"Open Project", "Ctrl+O"}, {"New Project", "Ctrl+N"}, {"Exit"}}});
            menus.push_back({"Edit", {{"Setting"}}});
            menus.push_back({"Window"});
            menus.push_back({"Tools"});
            components_.push_back(std::make_unique<EditorMainMenuBarComponent>(menus));

            //Camera
            std::unique_ptr<EditorUIComponent> camera_ui = std::make_unique<EditorCameraControlComponent>(kpengine::editor::global_editor_context.render_system_->GetRenderCamera());
            components_.push_back(std::move(camera_ui));

            std::unique_ptr<EditorUIComponent> actor_control_panel =std::make_unique<EditorActorControlPanel>(kpengine::editor::global_editor_context.level_system_->GetActor(0).get());
            components_.push_back(std::move(actor_control_panel));
        }

        void EditorUI::Close()
        {

            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

        void EditorUI::BeginDraw()
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

        }

        void EditorUI::EndDraw()
        {
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        }

        bool EditorUI::Render()
        {
            // Start the Dear ImGui frame
            for(int i = 0;i<components_.size();i++)
            {
                components_[i]->Render();
            }
            return true;
        }

        EditorUI::~EditorUI() = default;

    }
}