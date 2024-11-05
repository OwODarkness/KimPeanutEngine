#ifndef KPENGINE_EDITOR_WINDOW_COMPONENT_H
#define KPENGINE_EDITOR_WINDOW_COMPONENT_H

#include <vector>
#include <string>

#include "editor/include/editor_ui_component.h"


namespace kpengine{
    namespace ui{
        class EditorWindowComponent : public EditorUIComponent{

        public:
            EditorWindowComponent(const std::string& title);
            virtual ~EditorWindowComponent();
            virtual void Render() override;

            void AddComponent(EditorUIComponent* component);

        private:    
            std::string title_;
            std::vector<EditorUIComponent*> components_;
            bool is_open_ = true;
        };
    }
}

#endif