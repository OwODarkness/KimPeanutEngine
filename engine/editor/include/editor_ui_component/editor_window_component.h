#ifndef KPENGINE_EDITOR_WINDOW_COMPONENT_H
#define KPENGINE_EDITOR_WINDOW_COMPONENT_H

#include <vector>
#include <string>
#include <memory>
#include "editor/include/editor_ui_component.h"


namespace kpengine{
    namespace ui{
        class EditorWindowComponent : public EditorUIComponent{

        public:
            EditorWindowComponent(const std::string& title);
            virtual ~EditorWindowComponent();
            virtual void Render() override;
            virtual void RenderContent();
            void AddComponent(std::shared_ptr<EditorUIComponent> component);

        protected:    
            std::string title_;
            std::vector<std::shared_ptr<EditorUIComponent>> components_;
            bool is_open_ = true;
        public:
            int width_{};
            int height_{};
            float pos_x{};
            float pos_y{};
        };
    }
}

#endif