#include "editor_ui.h"

#include <string>
#include <iostream>
#include <cstdio>

#include "editor/include/editor_ui_component/editor_text_component.h"
#include "editor/include/editor_ui_component/editor_tooltip_component.h"
#include "editor/include/editor_ui_component/editor_button_component.h"
namespace kpengine
{
    namespace ui
    {
        EditorUI::EditorUI()
        {
            ui_components_.push_back(new EditorTextComponent("hello world, hello world"));
            ui_components_.push_back(new EditorTooltipComponent("welcome"));
            EditorButtonComponent* button = new EditorButtonComponent("按钮");
            button->BindClickEvent([](){std::cout << "click" << std::endl;});
            ui_components_.push_back(button);


        }
        EditorUI::~EditorUI()
        {
            for (int i = 0; i < ui_components_.size(); i++)
            {
                delete ui_components_[i];
                ui_components_[i] = nullptr;
            }
        }

        void EditorUI::Initialize(WindowInitInfo window_info)
        {
            glfwSetErrorCallback(&EditorUI::GLFWErrorCallback);

            glfwInit();
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            window_ = glfwCreateWindow(window_info.width, window_info.height, "KimPeanut Engine", nullptr, nullptr);
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(1);
            gladLoadGL();

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
            ImFont* font = io.Fonts->AddFontFromFileTTF("c:/windows/Fonts/msyh.ttc",
             24.0f,
            nullptr,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

            IM_ASSERT(font != nullptr);
            // Setup Dear ImGui style
            ImGui::StyleColorsDark();

            ImGui_ImplGlfw_InitForOpenGL(window_, true);
            const char *glsl_version = "#version 130";
            ImGui_ImplOpenGL3_Init(glsl_version);

            clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        }

        void EditorUI::Close()
        {

            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();

            glfwDestroyWindow(window_);
            glfwTerminate();
        }

        bool EditorUI::Render()
        {
            if (glfwWindowShouldClose(window_))
            {
                return false;
            }
            glfwPollEvents();

            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // kpengine::ui::RenderUI();

            ImGui::Begin("新的窗口");

            //render component
            for (int i = 0; i < ui_components_.size(); i++)
            {
                ui_components_[i]->Render();
            }

            ImGui::End();

            // Rendering
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window_, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color_.x * clear_color_.w, clear_color_.y * clear_color_.w, clear_color_.z * clear_color_.w, clear_color_.w);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window_);

            return true;
        }

        void EditorUI::GLFWErrorCallback(int error, const char *desc)
        {

            fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
        }

        void RenderUI()
        {
            // 创建一个设置窗口

            ImGui::Begin("设置拖拽按钮");
            // 按钮在单击时返回true（大多数小部件在编辑/激活时返回true）

            ImGui::Text("This is a text");
            ImGui::Text("hello world %d", 123);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "hello");
            ImGui::TextDisabled("text disabled");
            ImGui::TextWrapped("text wrapped");
            ImGui::LabelText("label", "context");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("hip1");
            }
            ImGui::BulletText("123123");

            float data[] = {1, 2, 3, 4, 5};
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("hip");
            }
            ImGui::PlotLines("Curve", data, sizeof(data));

            ImGui::End();
        }

    }
}