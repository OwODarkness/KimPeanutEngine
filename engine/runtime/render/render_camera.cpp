#include "render_camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
namespace kpengine
{
    glm::vec3 RenderCamera::up_{0.f, 1.f, 0.f};
    void RenderCamera::Move(const glm::vec3 &delta)
    {
        position_ += delta * speed_;
        
    }


    glm::mat4x4 RenderCamera::GetViewMatrix()
    {
        return glm::lookAt(position_, position_ + direction_, up_);
    }
}