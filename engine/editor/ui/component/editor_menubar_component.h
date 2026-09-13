#ifndef KPENGINE_EDITOR_MENUBAR_COMPONENT_H
#define KPENGINE_EDITOR_MENUBAR_COMPONENT_H

#include "editor/ui/component/editor_ui_component.h"

#include <functional>
#include <string>
#include <vector>

namespace kpengine::editor
{
    class EditorMenuComponent : public EditorUIComponent
    {
    public:
        virtual void Render() override;
    };
    struct MenuItem
    {

        std::string title;
        std::string short_cut;
        bool enabled = true;
        // Command binding: invoked once when the item is activated.
        std::function<void()> on_click;
        // Live checkmark binding, queried every frame so the menu cannot drift out
        // of sync with the state it reflects (a tab close, a panel's own X). Empty
        // renders unchecked.
        std::function<bool()> is_selected;
    };

    struct Menu
    {
        std::string title;
        std::vector<MenuItem> items;
        bool enabled = true;
    };

    class EditorMenuBarComponent : public EditorUIComponent
    {
    public:
        EditorMenuBarComponent(const std::vector<Menu> &menus);
        ~EditorMenuBarComponent();
        void AddMenu(const Menu &menu);
        virtual void Render() override;

    private:
        std::vector<Menu> menus_;
    };

    class EditorMainMenuBarComponent : public EditorMenuBarComponent
    {
    public:
        EditorMainMenuBarComponent(const std::vector<Menu> &menus);

        void Render() override;
    };
}

#endif