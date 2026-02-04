#include "camera.h"
#include <GLFW/glfw3.h>
#include "log/logger.h"

namespace kpengine
{
    void Camera::Initialize(class input::InputContext *input_context)
    {
        root_component_ = std::make_shared<CameraComponent>();
        camera_comp = dynamic_cast<CameraComponent *>(root_component_.get());

        if (input_context)
        {
            // rotate
            rotate_action = std::make_shared<input::InputAction>();
            rotate_action->callback_ = std::bind(&Camera::OnCameraRotateCallback, this, std::placeholders::_1);
            rotate_action->name_ = "CameraRotate";
            rotate_action->value_type_ = input::InputValueType::Axis2D;
            input_context->Bind(rotate_action, {input::InputDevice::Mouse, KPENGINE_MOUSE_CURSOR});

            forward_action = std::make_shared<input::InputAction>();
            forward_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            forward_action->name_ = "CameraForward";
            forward_action->value_type_ = input::InputValueType::Axis2D;
            forward_action->default_value = Vector3f{1.f, 0.f, 0.f};
            input_context->Bind(forward_action, {input::InputDevice::Keyboard, GLFW_KEY_W});

            backward_action = std::make_shared<input::InputAction>();
            backward_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            backward_action->name_ = "CameraBackward";
            backward_action->value_type_ = input::InputValueType::Axis2D;
            backward_action->default_value = Vector3f{-1.f, 0.f, 0.f};
            input_context->Bind(backward_action, {input::InputDevice::Keyboard, GLFW_KEY_S});

            right_action = std::make_shared<input::InputAction>();
            right_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            right_action->name_ = "CameraRight";
            right_action->value_type_ = input::InputValueType::Axis2D;
            right_action->default_value = Vector3f{0.f, 1.f, 0.f};
            input_context->Bind(right_action, {input::InputDevice::Keyboard, GLFW_KEY_D});

            left_action = std::make_shared<input::InputAction>();
            left_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            left_action->name_ = "CameraLeft";
            left_action->value_type_ = input::InputValueType::Axis2D;
            left_action->default_value = Vector3f{0.f, -1.f, 0.f};
            input_context->Bind(left_action, {input::InputDevice::Keyboard, GLFW_KEY_A});

            up_action = std::make_shared<input::InputAction>();
            up_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            up_action->name_ = "CameraUp";
            up_action->value_type_ = input::InputValueType::Axis2D;
            up_action->default_value = Vector3f{0.f, 0.f, 1.f};
            input_context->Bind(up_action, {input::InputDevice::Keyboard, GLFW_KEY_E});

            down_action = std::make_shared<input::InputAction>();
            down_action->callback_ = std::bind(&Camera::OnCameraMoveCallback, this, std::placeholders::_1);
            down_action->name_ = "CameraDown";
            down_action->value_type_ = input::InputValueType::Axis2D;
            down_action->default_value = Vector3f{0.f, 0.f, -1.f};
            input_context->Bind(down_action, {input::InputDevice::Keyboard, GLFW_KEY_Q});

            scroll_action = std::make_shared<input::InputAction>();
            scroll_action->callback_ = std::bind(&Camera::OnCameraScrollCallback, this, std::placeholders::_1);
            scroll_action->name_ = "CameraScroll";
            scroll_action->value_type_ = input::InputValueType::Axis1D;
            input_context->Bind(scroll_action, {input::InputDevice::Mouse, KPENGINE_MOUSE_SCROLL});
        }
    }

    CameraData Camera::GetCameraData() const  
    {
        return camera_comp->GetCameraData();
    }

    void Camera::MoveForward(float delta)
    {
        Vector3f delta_dir = camera_comp->GetCameraForward() * delta * move_speed_ * move_sensitivity_;
        AddRelativeOffset(delta_dir);
    }
    void Camera::MoveRight(float delta)
    {
        Vector3f delta_dir = camera_comp->GetCameraRight() * delta * move_speed_ * move_sensitivity_;
        AddRelativeOffset(delta_dir);
    }
    void Camera::MoveUp(float delta)
    {
        Vector3f delta_dir = camera_comp->GetCameraUp() * delta * move_speed_ * move_sensitivity_;
        AddRelativeOffset(delta_dir);
    }
    void Camera::Rotate(float theta, float phi)
    {
        Rotatorf rotation = GetActorRotation();
        float delta_pitch = theta * rotate_speed_ * rotate_sensitivity_;
        float delta_yaw = phi * rotate_speed_ * rotate_sensitivity_;

        float pitch = std::clamp(rotation.pitch_ + delta_pitch, K_PITCH_MIN, K_PITCH_MAX);
        float yaw = (float)((int)(rotation.yaw_ + delta_yaw) % 360);

        rotation.pitch_ = pitch;
        rotation.yaw_ = yaw;
        SetActorRotation(rotation);
    }

    void Camera::OnCameraRotateCallback(const input::InputState &state)
    {

        Vector2f res = std::get<Vector2f>(state.value);
        Rotate(-res.y_, res.x_);
    }

    void Camera::OnCameraMoveCallback(const input::InputState &state)
    {
        Vector3f res = std::get<Vector3f>(state.value);
        MoveForward(res.x_);
        MoveRight(res.y_);
        MoveUp(res.z_);

    }

    void Camera::OnCameraScrollCallback(const input::InputState &state)
    {

        float res = std::get<float>(state.value);
        MoveForward(res);
    }
}