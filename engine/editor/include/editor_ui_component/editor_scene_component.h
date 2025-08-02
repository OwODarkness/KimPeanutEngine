#ifndef KPENGINE_EDITOR_SCENE_COMPONENT_H
#define KPENGINE_EDITOR_SCENE_COMPONENT_H

#include <string>
#include <memory>

#include "runtime/core/system/delegate.h"
#include "editor/include/editor_ui_component/editor_window_component.h"

DECLARE_DELEGATE_TwoParams(FOnMouseClickCallback, float, float)

namespace kpengine{
    class FrameBuffer;
    namespace ui{
        class EditorSceneComponent : public EditorWindowComponent{
        public:
            //EditorSceneComponent() = delete;
            explicit EditorSceneComponent(FrameBuffer* scene);
            virtual ~EditorSceneComponent();
            virtual void RenderContent() override;

            void SetTitle(const std::string & title);
            float GetMousePosX() const{return mouse_pos_x_;}
            float GetMousePosY() const{return mouse_pos_y_;}
            bool IsLeftMouseDrag() const{return is_left_mouse_drag_;}
            bool IsLeftMouseDown() const{return is_left_mouse_down_;}
        private:
            std::shared_ptr<FrameBuffer> scene_;
            float mouse_pos_x_;
            float mouse_pos_y_;
            bool is_left_mouse_drag_;
            bool is_left_mouse_down_;

        public:
            bool is_scene_window_focus = false;
            FOnMouseClickCallback on_mouse_click_callback_;
        };
    }
}

#endif