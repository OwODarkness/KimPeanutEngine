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

    spatial::Ray RenderCamera::BuildWorldRay(float ndc_x, float ndc_y,
                                             float viewport_aspect) const
    {
        const float effective_aspect = viewport_aspect > 0.0f ? viewport_aspect : aspect_;
        if (projection_mode_ == CameraProjectionMode::Orthographic)
        {
            const float half_height = orthographic_height_ * 0.5f;
            const float half_width = half_height * effective_aspect;
            const Vector3f ray_origin = position_ + right_ * (ndc_x * half_width) +
                                         up_ * (ndc_y * half_height);
            return {ray_origin, forward_};
        }

        const float half_fov_tangent = std::tan(math::DegreeToRadian(fov_) * 0.5f);
        const Vector3f ray_direction =
            (forward_ + right_ * (ndc_x * half_fov_tangent * effective_aspect) +
             up_ * (ndc_y * half_fov_tangent))
                .GetSafetyNormalize();
        return {position_, ray_direction};
    }

    std::optional<Vector3f> RenderCamera::ProjectWorldPoint(
        const Vector3f &world_point, float viewport_aspect) const
    {
        if (!std::isfinite(viewport_aspect) || viewport_aspect <= 0.0f)
            return std::nullopt;
        RenderCamera camera = *this;
        camera.SetAspect(viewport_aspect);
        const Vector4f clip = camera.GetViewProjectionMatrix() * Vector4f(world_point, 1.0f);
        if (!std::isfinite(clip.x_) || !std::isfinite(clip.y_) ||
            !std::isfinite(clip.z_) || !std::isfinite(clip.w_) || clip.w_ <= 0.0001f)
            return std::nullopt;
        const float inverse_w = 1.0f / clip.w_;
        return Vector3f{clip.x_ * inverse_w, clip.y_ * inverse_w, clip.z_ * inverse_w};
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
