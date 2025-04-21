#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_H

#include "runtime/core/math/math_header.h"

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
    RenderCamera() = default;

    void MoveForward(float delta);

    void MoveRight(float delta);

    void MoveUp(float delta);

    //(theta, fhi)
    void Rotate(const Vector2f& delta);

    inline Vector3f GetPosition()const {return position_;}

    inline Vector3f GetDirection() const{return direction_;}

    Vector3f GetCameraRight() const;
    Matrix4f GetViewMatrix() const;

    Matrix4f GetProjectionMatrix() const;

private:
    Vector3f position_{0.f, 0.8f, 2.f};
    Vector3f direction_{0.f, 0.f, -1.f};
    static Vector3f up_;
    
    float move_coff_ = 0.05f;
    float rotate_coff_ = 0.05f;
    float aspect_ {4.f/3.f};
    float pitch_ = 0.f;
    float yaw_ = -90.f;

    CameraType camera_type_ = CameraType::CAMREA_PERSPECTIVE;

public:
    float fov_{45.f};
    float move_speed_ = 1.f;
    float rotate_speed_ = 1.f;
    float z_near_ = 0.1f;
    float z_far_ = 100.f;
};
}

#endif