#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_H

#include <glm/glm.hpp>

namespace kpengine{

enum class CameraType: unsigned int{
    CAMREA_PERSPECTIVE,
    CAMERA_ORTHOGONAL
};

class RenderCamera{

public:
    RenderCamera() = default;

    void MoveForward(float delta);

    void MoveRight(float delta);

    void MoveUp(float delta);

    //(theta, fhi)
    void Rotate(const glm::vec2& delta);

    inline glm::vec3 GetPosition() const{return position_;}

    inline glm::vec3 GetDirection() const{return direction_;}

    glm::vec3 GetCameraRight() const{return glm::cross(direction_, up_);}

    glm::mat4x4 GetViewMatrix();

    glm::mat4x4 GetProjectionMatrix();

private:
    glm::vec3 position_{0.f, 0.8f, 2.f};
    glm::vec3 direction_{0.f, 0.f, -1.f};
    
    float move_speed_ = 0.05f;
    float rotate_speed_ = 0.3f;
    float aspect_ {4.f/3.f};
    float fov_{45.f};
    static glm::vec3 up_;
    float pitch_ = 0.f;
    float yaw_ = -90.f;
    float z_near = 0.1f;
    float z_far = 100.f;
    const float pitch_min_ = -89.f;
    const float pitch_max_ = 89.f;

    CameraType camera_type_ = CameraType::CAMREA_PERSPECTIVE;
};
}

#endif