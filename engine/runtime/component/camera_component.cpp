#include "camera_component.h"

namespace kpengine
{
    Vector3f CameraComponent::world_up = {0.f, 1.f, 0.f};

    void CameraComponent::SetRelativeRotation(const Rotatorf &new_rotator)
    {
        SceneComponent::SetRelativeRotation(new_rotator);
        Update();
    }

    void CameraComponent::Update()
    {
        Rotatorf rotation = GetWorldRotation();
        float radian_pitch = math::DegreeToRadian(rotation.pitch_);
        float radian_yaw = math::DegreeToRadian(rotation.yaw_);

        Vector3f dir{};
        dir.x_ = std::cos(radian_pitch) * std::cos(radian_yaw);
        dir.y_ = std::sin(radian_pitch);
        dir.z_ = std::cos(radian_pitch) * std::sin(radian_yaw);

        forward_ = dir.GetSafetyNormalize();
        right_ = forward_.CrossProduct(world_up).GetSafetyNormalize();
        up_ = right_.CrossProduct(forward_).GetSafetyNormalize();
    }

    CameraData CameraComponent::GetCameraData() const
    {
        return {CalculateViewMatrix().Transpose(), CalculateProjectionMatrix().Transpose()};
    }

    Matrix4f CameraComponent::CalculateViewMatrix() const
    {
        return Matrix4f::MakeCameraMatrix(GetWorldLocation(), GetCameraForward(), GetCameraUp());
    }
    Matrix4f CameraComponent::CalculateProjectionMatrix() const
    {
        return Matrix4f::MakePerProjMatrix(math::DegreeToRadian(fov_), aspect_, near_, far_);
    }

    void CameraComponent::SetCameraAspect(float aspect)
    {
        aspect_ = aspect;
    }
    void CameraComponent::SetCameraNearPlane(float near)
    {
        near_ = near;
    }
    void CameraComponent::SetCameraFarPlane(float far)
    {
        far_ = far;
    }
}