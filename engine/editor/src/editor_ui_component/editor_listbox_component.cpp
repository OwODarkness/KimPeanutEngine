#include "editor_listbox_component.h"
#include "editor/include/editor_global_context.h"
#include "runtime/core/system/render_system.h"

#include <iostream>
namespace kpengine{
namespace  ui{

    EditorListboxComponent::EditorListboxComponent(std::vector<const char*> list_items):
    list_items_(list_items)
    {

    }

    void EditorListboxComponent::Render()
    {
        last_index = current_index;
        ImGui::ListBox("Shader Mode", &current_index, list_items_.data(), list_items_.size(), 2);
        if(last_index != current_index)
        {
            OnItemSelected(last_index, current_index);
        }
    }

    void EditorListboxComponent::OnItemSelected(int old_index, int new_index)
    {
        
        editor::global_editor_context.render_system_->SetCurrentShaderMode(list_items_[new_index]);
    }


}
}