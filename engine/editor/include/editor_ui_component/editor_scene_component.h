#ifndef KPENGINE_EDITOR_EDITOR_SCENE_COMPONENT_H
#define KPENGINE_EDITOR_EDITOR_SCENE_COMPONENT_H

#include <string>
#include <memory>
#include "editor/include/editor_ui_component.h"

namespace kpengine{
    class FrameBuffer;
    namespace ui{
        class EditorSceneComponent : public EditorUIComponent{
        public:
            EditorSceneComponent() = delete;
            EditorSceneComponent(FrameBuffer* scene);

            virtual void Render() override;

            bool ShouldResize(float new_width, float new_height);

            void SetTitle(const std::string & title);
        private:
            std::shared_ptr<FrameBuffer> scene_;
            float width_;
            float height_;
            std::string title_{"scene"};

        };
    }
}

#endif