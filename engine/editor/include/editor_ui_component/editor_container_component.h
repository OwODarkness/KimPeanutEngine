#ifndef KPENGINE_EDITOR_CONTAINER_COMPONENT_H
#define KPENGINE_EDITOR_CONTAINER_COMPONENT_H

#include <vector>

#include "editor/include/editor_ui_component.h"



namespace kpengine{
    namespace ui{
        class EditorContainerComponent: public EditorUIComponent{
            virtual ~EditorContainerComponent();
        public:
            void AddComponent(EditorUIComponent* component);
            virtual void Render() override;
        private:
            std::vector<EditorUIComponent*> items;


        };
    }
}

#endif