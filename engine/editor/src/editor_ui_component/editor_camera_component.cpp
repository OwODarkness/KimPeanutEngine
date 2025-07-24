#include "editor_camera_component.h"

#include <memory>

#include "runtime/render/render_camera.h"
#include "editor_text_component.h"
#include "editor_drag_component.h"
#include "editor_button_component.h"
#include "editor_text_component.h"

#include "runtime/core/system/delegate.h"
namespace kpengine{
    namespace ui{
        EditorCameraControlComponent::EditorCameraControlComponent(kpengine::RenderCamera* camera)
        :EditorWindowComponent("Camera Settings"), camera_(camera)
        {
            fov_default_ = camera->fov_;
            move_speed_default_ = camera->move_speed_;
            rotate_speed_default_ = camera->rotate_speed_;
            z_near_default_ = camera->z_near_;
            z_far_default_ = camera->z_far_;


            std::shared_ptr<EditorButtonComponent> btn = std::make_shared<EditorButtonComponent>("default");
            btn->on_click_notify_.Bind<EditorCameraControlComponent, &EditorCameraControlComponent::ResetCameraConfig>(this);
            AddComponent(btn);
            std::shared_ptr<EditorUIComponent> fov_drag = std::make_shared<EditorDragComponent<float>>("fov_setting", &camera->fov_, 1.f, 10.f, 160.f);
            AddComponent(fov_drag);

            std::shared_ptr<EditorUIComponent> move_drag = std::make_shared<EditorDragComponent<float>>("move_setting", &camera->move_speed_, 0.1f, move_speed_default_, 5 * move_speed_default_);
            AddComponent(move_drag);

            std::shared_ptr<EditorUIComponent> rotate_drag = std::make_shared<EditorDragComponent<float>>("rotate_setting", &camera->rotate_speed_, 0.1f,  rotate_speed_default_, 5 * rotate_speed_default_);
            AddComponent(rotate_drag);
            std::shared_ptr<EditorUIComponent> yaw_text_comp = std::make_shared<EditorDynamicTextComponent<float>>(&camera->yaw_, "yaw degree: ");
            AddComponent(yaw_text_comp);
            std::shared_ptr<EditorUIComponent> pitch_text_comp = std::make_shared<EditorDynamicTextComponent<float>>(&camera->pitch_, "pitch degree: ");
            AddComponent(pitch_text_comp);


        }

        void EditorCameraControlComponent::ResetCameraConfig()
        {
            camera_->fov_ = fov_default_;
            camera_->move_speed_ = move_speed_default_;
            camera_->rotate_speed_ = rotate_speed_default_;
            camera_->z_near_ = z_near_default_;
            camera_->z_far_ = z_far_default_;
        }
    }
}