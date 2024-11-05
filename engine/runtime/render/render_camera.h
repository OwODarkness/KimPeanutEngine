#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_H

#include <glm/glm.hpp>

namespace kpengine{

class RenderCamera{

public:
    RenderCamera() = default;

    void Move(const glm::vec3& delta);

    //(theta, fhi)
    void Rotate(const glm::vec2& delta);

    glm::vec3 GetPosition() const{return position_;}

    glm::mat4x4 GetViewMatrix();

private:
    glm::vec3 position_{0.f, 0.f, 0.f};
    glm::vec3 direction_{0.f, 0.f, -1.f};
public:
    float move_speed_ = 1.f;
    float rotate_speed = 1.f;
    float aspect_;
    float fov_{89.f};
    static glm::vec3 up_;
    float pitch_ = 0.f;
    float yaw_ = -90.f;
    const float pitch_min_ = -89.f;
    const float pitch_max_ = 89.f;
};
}

#endif