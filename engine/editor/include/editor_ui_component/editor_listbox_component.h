#ifndef KPENGINE_EDITOR_LISTBOX_COMPONENT_H
#define KPENGINE_EDITOR_LISTBOX_COMPONENT_H

#include "editor_ui_component.h"

#include <vector>

namespace kpengine{
namespace ui{

    class EditorListboxComponent: public EditorUIComponent{
    public:
        EditorListboxComponent(std::vector<const char*> list_items);
        void Render() override;
        void OnItemSelected(int old_index, int new_index);

    private:    
        std::vector<const char*> list_items_;
        int current_index = 0;
        int last_index = 0;
    };

}
}

#endif