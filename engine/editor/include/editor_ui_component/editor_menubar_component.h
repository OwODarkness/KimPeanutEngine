#ifndef KPENGINE_EDITOR_MENUBAR_COMPONENT_H
#define KPENGINE_EDITOR_MENUBAR_COMPONENT_H

#include "editor/include/editor_ui_component.h"

#include <string>
#include <vector>

namespace kpengine{
    namespace ui{
        class EditorMenuComponent: public EditorUIComponent{
            public:
            virtual void Render() override;
        };
        struct MenuItem {
            
            std::string title;
            std::string short_cut;
            bool selected = false;
            bool enabled = true;
        };

        struct Menu{
            std::string title;
            std::vector<MenuItem> items;
            bool enabled = true;
        };

        class EditorMenuBarComponent: public EditorUIComponent{
        public:
            EditorMenuBarComponent(const std::vector<Menu>& menus);
            ~EditorMenuBarComponent();
            void AddMenu(const Menu&menu);
            virtual void Render() override;
        private:
            std::vector<Menu> menus_;
        };

        class EditorMainMenuBarComponent: public EditorMenuBarComponent{
        public:
            EditorMainMenuBarComponent(const std::vector<Menu>& menus);
            
            void Render() override;
        };
    }
}


#endif