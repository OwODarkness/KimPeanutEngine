#include "editor_window_component.h"
#include <iostream>
namespace kpengine
{
    namespace ui
    {

        EditorWindowComponent::EditorWindowComponent(const std::string &title) : title_(title) {}

        EditorWindowComponent::~EditorWindowComponent()
        {
            for (int i = 0; i < components_.size(); i++)
            {
                delete components_[i];
            }
            components_.clear();
        }

        void EditorWindowComponent::Render()
        {
            if (is_open_)
            {
                ImGui::Begin(title_.c_str(), &is_open_);
                RenderContent();
                ImGui::End();
            }
        }

        void EditorWindowComponent::RenderContent()
        {
            pos_x = ImGui::GetWindowPos().x;
            pos_y = ImGui::GetWindowPos().y;
            width_ = (int)ImGui::GetContentRegionAvail().x;
            height_ = (int)ImGui::GetContentRegionAvail().y;
            
            for (int i = 0; i < components_.size(); i++)
            {
                components_[i]->Render();
            }
        }

        void EditorWindowComponent::AddComponent(EditorUIComponent *component)
        {
            components_.push_back(component);
        }
    }
}