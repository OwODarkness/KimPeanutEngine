#include "editor_container_component.h"

namespace kpengine{
    namespace ui{

        EditorContainerComponent::~EditorContainerComponent()
        {
            for(int i = 0;i< items.size();i++)
            {
                delete items[i];
            }
        }
        
        void EditorContainerComponent::AddComponent(EditorUIComponent* component)
        {
            if(component == nullptr)
            {
                return ;
            }
            items.push_back(component);
        }

        void EditorContainerComponent::Render()
        {
            for(int i = 0;i<items.size()-1;i++)
            {
                items[i]->Render();
                ImGui::SameLine();
            }
            items[items.size() - 1]->Render();
        }

    }
    
    }
