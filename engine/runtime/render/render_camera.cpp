#include "render_camera.h"

#include <iostream>
#include <cmath>
#include <algorithm>


#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"


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

    void RenderCamera::Rotate(const Vector2f &delta)
    {
        float delta_pitch = delta.x_ * rotate_speed_ * rotate_coff_;
        float delta_yaw = delta.y_ * rotate_speed_ * rotate_coff_;
    
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
}