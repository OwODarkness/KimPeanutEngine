#ifndef KPENGINE_EDITOR_SCENE_COMPONENT_H
#define KPENGINE_EDITOR_SCENE_COMPONENT_H

#include <string>
#include <memory>
#include "editor/include/editor_ui_component/editor_window_component.h"
namespace kpengine{
    class FrameBuffer;
    namespace ui{
        class EditorSceneComponent : public EditorWindowComponent{
        public:
            //EditorSceneComponent() = delete;
            EditorSceneComponent(FrameBuffer* scene);
            virtual ~EditorSceneComponent();
            virtual void RenderContent() override;

            void SetTitle(const std::string & title);
        private:
            std::shared_ptr<FrameBuffer> scene_;
        public:


            bool is_scene_window_focus;
        };
    }
}

#endif