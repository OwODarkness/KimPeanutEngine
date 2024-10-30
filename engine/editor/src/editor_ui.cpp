#include "editor_ui.h"

#include <string>
#include <iostream>
#include <cstdio>

#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_tooltip_component.h"
#include "editor/include/editor_ui_component/editor_button_component.h"
#include "editor/include/editor_ui_component/editor_menubar_component.h"

#include "shader/shader.h"
#include "runtime/core/base/Mesh.h"
#include "runtime/render/frame_buffer.h"
namespace kpengine
{
    namespace ui
    {
        EditorUI::EditorUI()
        {
        }
        EditorUI::~EditorUI()
        {
            for (int i = 0; i < ui_components_.size(); i++)
            {
                delete ui_components_[i];
                ui_components_[i] = nullptr;
            }
           delete main_menubar_;
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




            ui_components_.push_back(new EditorTextComponent("hello imgui"));
            ui_components_.push_back(new EditorTooltipComponent("welcome"));
            EditorButtonComponent *button = new EditorButtonComponent("button");
            button->BindClickEvent([]()
                                   { std::cout << "click" << std::endl; });
            ui_components_.push_back(button);

            //menu init
            std::vector<Menu> menus;
            menus.push_back({"File", {{"Open Project", "Ctrl+O"}, {"New Project", "Ctrl+N"}, {"Exit"}}});
            menus.push_back({"Edit", {{"Setting"}}});
            menus.push_back({"Window"});
            menus.push_back({"Tools"});
            main_menubar_ = new EditorMenuBarComponent(menus);
        
            shader = new kpengine::ShaderHelper("normal.vs", "normal.fs");
            shader->Initialize();

            std::vector<Vertex> verticles = {
                {{0.5, -0.5, 0}},
                {{-0.5, -0.5, 0}},
                {{0, 0.5, 0}}
            };
            std::vector<unsigned int> indices= {0, 1, 2};
            mesh = new kpengine::Mesh(verticles, indices);
            mesh->Initialize();

            int window_width, window_height;
            glfwGetWindowSize(window, &window_width, &window_height);
            frame_buffer = std::make_shared<FrameBuffer>(window_width, window_height);
            frame_buffer->Initialize();

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


            ImGui::Begin("new window");
            // render component
            for (int i = 0; i < ui_components_.size(); i++)
            {
                ui_components_[i]->Render();
            }
            ImGui::End();


            frame_buffer->BindFrameBuffer();
            shader->UseProgram();
            mesh->Draw();
            ImGui::Begin("Scene");
            {
                
                ImGui::BeginChild("Render");
                float width = ImGui::GetContentRegionAvail().x;
                float height = ImGui::GetContentRegionAvail().y;
                ImGui::Image(
                    (ImTextureID)frame_buffer->GetTexture(),
                    ImGui::GetContentRegionAvail(),
                    ImVec2(0, 1),
                    ImVec2(1, 0)
                );
                ImGui::EndChild();
            }
            ImGui::End();
            frame_buffer->UnBindFrameBuffer();


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