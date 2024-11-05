#ifndef KPENGINE_EDITOR_SCENE_COMPONENT_H
#define KPENGINE_EDITOR_SCENE_COMPONENT_H

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


            void SetTitle(const std::string & title);
        private:
            std::shared_ptr<FrameBuffer> scene_;

            std::string title_{"scene"};
        public:
            int width_;
            int height_;
            float pos_x;
            float pos_y;
        };
    }
}

#endif