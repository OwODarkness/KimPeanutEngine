#include "editor_menubar_component.h"

namespace kpengine{
    namespace ui
    {

        EditorMenuBarComponent::EditorMenuBarComponent(const std::vector<Menu>& menus):
        menus_(menus)
        {

        }

        EditorMenuBarComponent::~EditorMenuBarComponent()
        {
            menus_.clear();
        }

        void EditorMenuBarComponent::AddMenu(const Menu& menu)
        {
            this->menus_.push_back(menu);
        }

        void EditorMenuBarComponent::Render()
        {
            for(Menu& menu: menus_)
            {
                if(ImGui::BeginMenu(menu.title.c_str(), menu.enabled))
                {
                    for(MenuItem& menu_item : menu.items)
                    {
                        ImGui::MenuItem(menu_item.title.c_str(), menu_item.short_cut.c_str(), &menu_item.selected, menu_item.enabled);
                    }
                    ImGui::EndMenu();
                }
            }
        }

        EditorMainMenuBarComponent::EditorMainMenuBarComponent(const std::vector<Menu>& menus):
        EditorMenuBarComponent(menus)
        {

        }

        void EditorMainMenuBarComponent::Render()
        {
            ImGui::BeginMainMenuBar();
            EditorMenuBarComponent::Render();
            ImGui::EndMainMenuBar();
        }
    }
}