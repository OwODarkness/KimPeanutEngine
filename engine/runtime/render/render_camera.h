#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_H

#include "math/math_header.h"
#include "input/input_action.h"
#include <memory>

namespace kpengine::input{
    class InputAction;
}

namespace kpengine{
    constexpr float pitch_min_ = -89.f;
    constexpr float pitch_max_ = 89.f;

enum class CameraType: unsigned int{
    CAMREA_PERSPECTIVE,
    CAMERA_ORTHOGONAL
};

//TODO replaced original third-party lib glm data type by custom matrix which could be found in runtime/core/math
class RenderCamera{

public:
    RenderCamera();

    void Initialize();

    void PostInitialize();

    void MoveForward(float delta);

    void MoveRight(float delta);

    void MoveUp(float delta);

    //(theta, fhi)
    void Rotate(float delta_x, float delta_y);

    inline const Vector3f GetPosition()const {return position_;}

    Vector3f& GetPosition() {return position_;}

    inline const Vector3f GetDirection() const{return direction_;}
    Vector3f GetCameraUp() const{return up_;}
    Vector3f GetCameraRight() const{return right_;};
    Vector3f GetCameraForward() const{return direction_;}
    Matrix4f GetViewMatrix() const;
    Matrix4f GetProjectionMatrix() const;
    float GetCameraFOV() const{return fov_;}
    Vector3f ComputeWorldRayFromScreen(const Vector2f& mouse_pos, const Vector2f& window_size) const;
private:
    void OnCameraRotateCallback(const input::InputState& state);
    void OnCameraMoveCallback(const input::InputState& state);
    void OnCameraScrollCallback(const input::InputState& state);


private:
    Vector3f position_;
    Vector3f direction_;
    Vector3f up_;
    Vector3f right_;
    static Vector3f world_up;
    float move_coff_ = 0.05f;
    float rotate_coff_ = 0.05f;
    float aspect_ {4.f/3.f};

    CameraType camera_type_ = CameraType::CAMREA_PERSPECTIVE;
    bool is_lock_ = false;

    std::shared_ptr<input::InputAction> rotate_action;
    std::shared_ptr<input::InputAction> forward_action;
    std::shared_ptr<input::InputAction> backward_action;
    std::shared_ptr<input::InputAction> right_action;
    std::shared_ptr<input::InputAction> left_action;
    std::shared_ptr<input::InputAction> up_action;
    std::shared_ptr<input::InputAction> down_action;
    std::shared_ptr<input::InputAction> scroll_action;
public:
    float fov_{45.f};
    float move_speed_ = 1.f;
    float rotate_speed_ = 1.f;
    float z_near_ = 0.1f;
    float z_far_ = 25.f;
    float pitch_ = 0.f;
    float yaw_ = -90.f;
};
}

#endif