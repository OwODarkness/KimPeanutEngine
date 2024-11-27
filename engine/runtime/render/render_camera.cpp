#include "render_camera.h"

#include <iostream>
#include <cmath>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
namespace kpengine
{
    glm::vec3 RenderCamera::up_{0.f, 1.f, 0.f};
    void RenderCamera::MoveForward(float delta)
    {
        position_ += direction_ * delta * move_speed_;
        // std::cout << "(" << position_.x << " ," << position_.y << " ," << position_.z << ")" << std::endl;
    }

    void RenderCamera::MoveRight(float delta)
    {
        position_ += GetCameraRight() * delta * move_speed_;
    }

    void RenderCamera::MoveUp(float delta)
    {
        position_ += up_ * delta * move_speed_;
    }

    void RenderCamera::Rotate(const glm::vec2 &delta)
    {
        float delta_pitch = delta.x * rotate_speed_;
        float delta_yaw = delta.y * rotate_speed_;

        yaw_ += delta_yaw;
        pitch_ = std::clamp(delta_pitch + pitch_, pitch_min_, pitch_max_);

        // std::cout << "yaw:" << yaw_ << " pitch:" << pitch_ << std::endl;

        glm::vec3 tmp_dir = glm::vec3(1);
        float radians_pitch = glm::radians(pitch_);
        float radians_yaw = glm::radians(yaw_);
        tmp_dir.x = std::cos(radians_pitch) * std::cos(radians_yaw);
        tmp_dir.y = std::sin(radians_pitch);
        tmp_dir.z = std::cos(radians_pitch) * std::sin(radians_yaw);
        direction_ = glm::normalize(tmp_dir);
    }

    glm::mat4x4 RenderCamera::GetViewMatrix()
    {
        return glm::lookAt(position_, position_ + direction_, up_);
    }

    glm::mat4x4 RenderCamera::GetProjectionMatrix()
    {
        if (CameraType::CAMREA_PERSPECTIVE == camera_type_)
        {
            const int kwidth = runtime::global_runtime_context.window_system_->width_;
            const int kheight = runtime::global_runtime_context.window_system_->height_;
            aspect_ = (float)(kwidth / kheight);
            return glm::perspective(glm::radians(fov_), aspect_, z_near, z_far);
        }
        else
        {
            return glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, z_near, z_far);
        }
    }
}