#include "render_camera.h"

#include <iostream>
#include <cmath>
#include <algorithm>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"


namespace kpengine
{

    
    

    Vector3f RenderCamera::up_{0.f, 1.f, 0.f};
    void RenderCamera::MoveForward(float delta)
    {
        position_ += direction_ * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::MoveRight(float delta)
    {
        position_ += GetCameraRight() * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::MoveUp(float delta)
    {
        position_ += up_ * delta * move_speed_ * move_coff_;
    }

    void RenderCamera::Rotate(const Vector2f &delta)
    {
        float delta_pitch = delta.x_ * rotate_speed_ * rotate_coff_;
        float delta_yaw = delta.y_ * rotate_speed_ * rotate_coff_;
    
        yaw_ += delta_yaw;
        pitch_ = std::clamp(delta_pitch + pitch_, pitch_min_, pitch_max_);
    
        float radians_pitch = math::DegreeToRadian(pitch_);
        float radians_yaw = math::DegreeToRadian(yaw_);
    
        Vector3f dir;
        dir.x_ = std::cos(radians_pitch) * std::cos(radians_yaw);
        dir.y_ = std::sin(radians_pitch);
        dir.z_ = std::cos(radians_pitch) * std::sin(radians_yaw);
    
        direction_ = dir.GetSafetyNormalize();

    }
    

    Vector3f RenderCamera::GetCameraRight() const
    {
        return direction_.CrossProduct(up_);
    }

   Matrix4f RenderCamera::GetViewMatrix() const
    {
        Matrix4f mat1 = Matrix4f::MakeCameraMatrix(position_, direction_, up_);

        return mat1;
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
}