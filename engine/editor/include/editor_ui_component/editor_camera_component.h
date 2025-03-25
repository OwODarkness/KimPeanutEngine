#ifndef KPENGINE_EDITOR_CAMERA_COMPONENT_H
#define KPENGINE_EDITOR_CAMERA_COMPONENT_H

#include "editor/include/editor_ui_component/editor_window_component.h"

namespace kpengine{
    
    class RenderCamera;
    namespace ui{
        class EditorCameraControlComponent:public EditorWindowComponent{
        public:
            EditorCameraControlComponent(kpengine::RenderCamera* camera);
        private:
            float fov_default_;
            float move_speed_default_;
            float rotate_speed_default_;
            float z_near_default_;
            float z_far_default_;

            RenderCamera* camera_;
            void ResetCameraConfig();
        };
    }
}
#endif