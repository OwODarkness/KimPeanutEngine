#include "editor_ui.h"

#include <string>
#include <iostream>
#include <cstdio>
#include <cmath>

#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_tooltip_component.h"
#include "editor/include/editor_ui_component/editor_button_component.h"
#include "editor/include/editor_ui_component/editor_menubar_component.h"
#include "editor/include/editor_ui_component/editor_window_component.h"
#include "editor/include/editor_ui_component/editor_plot_component.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor_ui_component/editor_container_component.h"
#include "editor/include/editor_ui_component/editor_camera_component.h"
#include "runtime/core/system/render_system.h"
#include "runtime/engine.h"


namespace kpengine
{
    namespace ui
    {
        EditorUI::EditorUI()
        {
        }
        EditorUI::~EditorUI()
        {

           for(int i = 0;i<components_.size();i++)
           {
                delete components_[i];
           }
        }

        void EditorUI::Initialize(GLFWwindow *window)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();
            // Setup Dear ImGui style
            ImGui_ImplGlfw_InitForOpenGL(window, true);
            const char *glsl_version = "#version 330";
            ImGui_ImplOpenGL3_Init(glsl_version);
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
            //ImFont *font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\simhei.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
            //IM_ASSERT(font != nullptr);

            EditorWindowComponent* window_component_ = new EditorWindowComponent("window");
            window_component_->AddComponent(new EditorTextComponent("hello imgui"));
            window_component_->AddComponent(new EditorTooltipComponent("welcome"));
            EditorButtonComponent *button = new EditorButtonComponent("button");
            button->BindClickEvent([]()
                                   { std::cout << "click" << std::endl; });
            window_component_->AddComponent(button);
            window_component_->AddComponent(new EditorPlotComponent([](float x){return std::sin(x);}, 0, 10));
           
            EditorContainerComponent* container = new EditorContainerComponent();
            container->AddComponent(new EditorTextComponent("FPS: "));
            container->AddComponent(new EditorDynamicTextComponent(&x));
            window_component_->AddComponent(container);
             
            components_.push_back(window_component_);

            //menu init
            std::vector<Menu> menus;
            menus.push_back({"File", {{"Open Project", "Ctrl+O"}, {"New Project", "Ctrl+N"}, {"Exit"}}});
            menus.push_back({"Edit", {{"Setting"}}});
            menus.push_back({"Window"});
            menus.push_back({"Tools"});
            components_.push_back(new EditorMainMenuBarComponent(menus));

            //Camera
            EditorWindowComponent* camera_ui = new EditorCameraControlComponent(kpengine::editor::global_editor_context.render_system_->GetRenderCamera());
            components_.push_back(camera_ui);
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
            x = editor::global_editor_context.runtime_engine_->GetFPS();
            for(int i = 0;i<components_.size();i++)
            {
                components_[i]->Render();
            }
            return true;
        }



    }
}