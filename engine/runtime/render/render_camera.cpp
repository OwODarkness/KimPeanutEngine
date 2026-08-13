#include "render_camera.h"

#include <cmath>

namespace kpengine::render
{
    Vector3f RenderCamera::world_up = {0.f, 1.f, 0.f};

    void RenderCamera::Update()
    {
        float radian_pitch = math::DegreeToRadian(rotation_.pitch_);
        float radian_yaw = math::DegreeToRadian(rotation_.yaw_);

        Vector3f dir;
        dir.x_ = std::cos(radian_pitch) * std::cos(radian_yaw);
        dir.y_ = std::sin(radian_pitch);
        dir.z_ = std::cos(radian_pitch) * std::sin(radian_yaw);

        forward_ = dir.GetSafetyNormalize();
        right_ = forward_.CrossProduct(world_up).GetSafetyNormalize();
        up_ = right_.CrossProduct(forward_).GetSafetyNormalize();
    }

    CameraData RenderCamera::GetCameraData() const
    {
        return {CalculateViewMatrix().Transpose(), CalculateProjectionMatrix().Transpose()};
    }

    Matrix4f RenderCamera::CalculateViewMatrix() const
    {
        return Matrix4f::MakeCameraMatrix(position_, forward_, up_);
    }

    Matrix4f RenderCamera::CalculateProjectionMatrix() const
    {
        return Matrix4f::MakePerProjMatrix(math::DegreeToRadian(fov_), aspect_, near_, far_);
    }
}
