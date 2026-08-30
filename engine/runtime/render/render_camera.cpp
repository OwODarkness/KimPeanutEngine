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

    Matrix4f RenderCamera::GetViewProjectionMatrix() const
    {
        return CalculateProjectionMatrix() * CalculateViewMatrix();
    }

    Matrix4f RenderCamera::CalculateViewMatrix() const
    {
        return Matrix4f::MakeCameraMatrix(position_, forward_, up_);
    }

    Matrix4f RenderCamera::CalculateProjectionMatrix() const
    {
        if (projection_mode_ == CameraProjectionMode::Orthographic)
        {
            const float half_height = orthographic_height_ * 0.5f;
            const float half_width = half_height * aspect_;
            return Matrix4f::MakeOrthProjMatrix(-half_width, half_width, -half_height,
                                                 half_height, near_, far_);
        }
        return Matrix4f::MakePerProjMatrix(math::DegreeToRadian(fov_), aspect_, near_, far_);
    }
}
