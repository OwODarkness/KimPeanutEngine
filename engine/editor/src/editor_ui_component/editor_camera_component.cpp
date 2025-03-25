#include "editor_camera_component.h"
#include "runtime/render/render_camera.h"
#include "editor_text_component.h"
#include "editor_slider_component.h"
#include "editor_button_component.h"
#include "runtime/core/system/delegate.h"
namespace kpengine{
    namespace ui{
        EditorCameraControlComponent::EditorCameraControlComponent(kpengine::RenderCamera* camera)
        :EditorWindowComponent("Camera control"), camera_(camera)
        {
            fov_default_ = camera->fov_;
            move_speed_default_ = camera->move_speed_;
            rotate_speed_default_ = camera->rotate_speed_;
            z_near_default_ = camera->z_near_;
            z_far_default_ = camera->z_far_;


            EditorButtonComponent* btn = new EditorButtonComponent("default");
            btn->on_click_notify_.Bind<EditorCameraControlComponent, &EditorCameraControlComponent::ResetCameraConfig>(this);
            AddComponent(btn);
            EditorSliderComponent<float>* fov_slider = new EditorSliderComponent<float>("fov_slider", &camera->fov_, 10.f, 160.f);
            AddComponent(fov_slider);

            EditorSliderComponent<float>* move_slider = new EditorSliderComponent<float>("move_slider", &camera->move_speed_, move_speed_default_, 5 * move_speed_default_);
            AddComponent(move_slider);

            EditorSliderComponent<float>* rotate_slider = new EditorSliderComponent<float>("rotate_slider", &camera->rotate_speed_, rotate_speed_default_, 5 * rotate_speed_default_);
            AddComponent(rotate_slider);
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