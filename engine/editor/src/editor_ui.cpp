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
#include "editor/include/editor_ui_component/eidtor_plot_component.h"
namespace kpengine
{
    namespace ui
    {
        EditorUI::EditorUI()
        {
        }
        EditorUI::~EditorUI()
        {

           delete main_menubar_;
           delete window_component_;
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

            window_component_ = new EditorWindowComponent("window");
            window_component_->AddComponent(new EditorTextComponent("hello imgui"));
            window_component_->AddComponent(new EditorTooltipComponent("welcome"));
            EditorButtonComponent *button = new EditorButtonComponent("button");
            button->BindClickEvent([]()
                                   { std::cout << "click" << std::endl; });
            window_component_->AddComponent(button);
            window_component_->AddComponent(new EditorPlotComponent([](float x){return std::sin(x);}, 0, 10));
            //menu init
            std::vector<Menu> menus;
            menus.push_back({"File", {{"Open Project", "Ctrl+O"}, {"New Project", "Ctrl+N"}, {"Exit"}}});
            menus.push_back({"Edit", {{"Setting"}}});
            menus.push_back({"Window"});
            menus.push_back({"Tools"});
            main_menubar_ = new EditorMenuBarComponent(menus);
        
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

            MainMenuBarRender();
        }

        void EditorUI::EndDraw()
        {
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        }

        bool EditorUI::Render()
        {
            // Start the Dear ImGui frame

            window_component_->Render();
            return true;
        }


        void EditorUI::MainMenuBarRender()
        {
            ImGui::BeginMainMenuBar();
            main_menubar_->Render();
            ImGui::EndMainMenuBar();
        }
    }
}