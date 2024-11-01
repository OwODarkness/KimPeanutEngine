#include "render_camera.h"

#include <cmath>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace kpengine
{
    glm::vec3 RenderCamera::up_{0.f, 1.f, 0.f};
    void RenderCamera::Move(const glm::vec3 &delta)
    {
        position_ += delta * speed_;
        
    }

    void RenderCamera::Rotate(const glm::vec2& delta)
    {
        int delta_pitch = delta.x;
        int delta_yaw = delta.y;

        yaw_ += delta_yaw;
        pitch_ = std::clamp(delta_pitch + pitch_, pitch_min_, pitch_max_);
    
        glm::vec3 tmp_dir = glm::vec3(1);
        tmp_dir.x = std::cos(pitch_) * std::cos(yaw_);
        tmp_dir.y = std::sin(pitch_);
        tmp_dir.z = std::cos(pitch_) * std::sin(yaw_);
        direction_ = glm::normalize(tmp_dir);
    }

    glm::mat4x4 RenderCamera::GetViewMatrix()
    {
        return glm::lookAt(position_, position_ + direction_, up_);
    }
}