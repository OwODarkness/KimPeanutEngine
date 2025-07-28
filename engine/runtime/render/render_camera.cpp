#include "render_camera.h"

#include <iostream>
#include <cmath>
#include <algorithm>


#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "editor/include/editor_global_context.h"
#include "runtime/input/input_context.h"
#include "runtime/core/system/input_system.h"

namespace kpengine
{

    Vector3f RenderCamera::world_up{0.f, 1.f, 0.f};
    
    RenderCamera::RenderCamera():
    position_({0.f, 0.8f, 2.f}),
    direction_({0.f, 0.f, -1.f}),
    up_({0.f, 1.f, 0.f})
    {
        right_ = direction_.CrossProduct(up_);
    }

    void RenderCamera::Initialize()
    {
        
    }

    void RenderCamera::PostInitialize()
    {
        std::shared_ptr<input::InputContext> input_context = runtime::global_runtime_context.input_system_->GetInputContext("SceneInputContext");
        if(input_context)
        {
            //rotate
            rotate_action = std::make_shared<input::InputAction>();
            rotate_action->callback_ = std::bind(&RenderCamera::OnCameraRotateCallback, this, std::placeholders::_1);
            rotate_action->name_ = "CameraRotate";
            rotate_action->value_type_ = input::InputValueType::Axis2D;
            input_context->Bind(rotate_action, {input::InputDevice::Mouse, KPENGINE_MOUSE_CURSOR});

            forward_action = std::make_shared<input::InputAction>();
            forward_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            forward_action->name_ = "CameraForward";
            forward_action->value_type_ = input::InputValueType::Axis2D;
            forward_action->default_value = Vector3f{1.f, 0.f, 0.f};
            input_context->Bind(forward_action, {input::InputDevice::Keyboard, GLFW_KEY_W});

            backward_action = std::make_shared<input::InputAction>();
            backward_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            backward_action->name_ = "CameraBackward";
            backward_action->value_type_ = input::InputValueType::Axis2D;
            backward_action->default_value = Vector3f{-1.f, 0.f, 0.f};
            input_context->Bind(backward_action, {input::InputDevice::Keyboard, GLFW_KEY_S});

            right_action = std::make_shared<input::InputAction>();
            right_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            right_action->name_ = "CameraRight";
            right_action->value_type_ = input::InputValueType::Axis2D;
            right_action->default_value = Vector3f{0.f, 1.f, 0.f};
            input_context->Bind(right_action, {input::InputDevice::Keyboard, GLFW_KEY_D});

            left_action = std::make_shared<input::InputAction>();
            left_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            left_action->name_ = "CameraLeft";
            left_action->value_type_ = input::InputValueType::Axis2D;
            left_action->default_value = Vector3f{0.f, -1.f, 0.f};
            input_context->Bind(left_action, {input::InputDevice::Keyboard, GLFW_KEY_A});

            up_action = std::make_shared<input::InputAction>();
            up_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            up_action->name_ = "CameraUp";
            up_action->value_type_ = input::InputValueType::Axis2D;
            up_action->default_value = Vector3f{0.f, 0.f, 1.f};
            input_context->Bind(up_action, {input::InputDevice::Keyboard, GLFW_KEY_E});

            down_action = std::make_shared<input::InputAction>();
            down_action->callback_ = std::bind(&RenderCamera::OnCameraMoveCallback, this, std::placeholders::_1);
            down_action->name_ = "CameraDown";
            down_action->value_type_ = input::InputValueType::Axis2D;
            down_action->default_value = Vector3f{0.f, 0.f, -1.f};
            input_context->Bind(down_action, {input::InputDevice::Keyboard, GLFW_KEY_Q});
        }
    }


    void RenderCamera::MoveForward(float delta)
    {
        position_ += direction_ * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::MoveRight(float delta)
    {
        position_ += right_ * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::MoveUp(float delta)
    {
        position_ += world_up * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::Rotate(float delta_x, float delta_y)
    {
        float delta_pitch = delta_x * rotate_speed_ * rotate_coff_;
        float delta_yaw = delta_y * rotate_speed_ * rotate_coff_;
    
        yaw_ = (float)((int)(yaw_ +  delta_yaw) % 360);
        pitch_ = std::clamp(delta_pitch + pitch_, pitch_min_, pitch_max_);
    
        float radians_pitch = math::DegreeToRadian(pitch_);
        float radians_yaw = math::DegreeToRadian(yaw_);
    
        Vector3f dir;
        dir.x_ = std::cos(radians_pitch) * std::cos(radians_yaw);
        dir.y_ = std::sin(radians_pitch);
        dir.z_ = std::cos(radians_pitch) * std::sin(radians_yaw);
    
        direction_ = dir.GetSafetyNormalize();

        right_ = direction_.CrossProduct(world_up).GetSafetyNormalize();
        up_ = right_.CrossProduct(direction_).GetSafetyNormalize();

    }
    

   Matrix4f RenderCamera::GetViewMatrix() const
    {
        return  Matrix4f::MakeCameraMatrix(position_, direction_, up_);
    }

    Matrix4f RenderCamera::GetProjectionMatrix() const
    {
        if (CameraType::CAMREA_PERSPECTIVE == camera_type_)
        {
            // const int kwidth = runtime::global_runtime_context.window_system_->width_;
            // const int kheight = runtime::global_runtime_context.window_system_->height_;
            // aspect_ = (float)(kwidth / kheight);
            return Matrix4f::MakePerProjMatrix(fov_, aspect_, z_near_, z_far_);
        }
        else
        {
            return Matrix4f::MakeOrthProjMatrix(-10.0f, 10.0f, -10.0f, 10.0f, z_near_, z_far_);
        }
    }

    void RenderCamera::OnCameraRotateCallback(const input::InputState& state)
    {
        if(is_lock_)
        {
            return ;
        }
        Vector2f res = std::get<Vector2f>(state.value);
        Rotate(-res.y_, res.x_);
    }

    void RenderCamera::OnCameraMoveCallback(const input::InputState& state)
    {
        if(is_lock_)
        {
            return ;
        }
        Vector3f res = std::get<Vector3f>(state.value);
        MoveForward(res.x_);
        MoveRight(res.y_);
        MoveUp(res.z_);

    }
}