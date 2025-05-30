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
    RenderCamera();

    void MoveForward(float delta);

    void MoveRight(float delta);

    void MoveUp(float delta);

    //(theta, fhi)
    void Rotate(const Vector2f& delta);

    inline const Vector3f GetPosition()const {return position_;}

    Vector3f& GetPosition() {return position_;}

    inline const Vector3f GetDirection() const{return direction_;}

    Vector3f GetCameraRight() const{return right_;};
    Matrix4f GetViewMatrix() const;

    Matrix4f GetProjectionMatrix() const;

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

public:
    float fov_{45.f};
    float move_speed_ = 1.f;
    float rotate_speed_ = 1.f;
    float z_near_ = 0.1f;
    float z_far_ = 100.f;
    float pitch_ = 0.f;
    float yaw_ = -90.f;
};
}

#endif