#ifndef KPENGINE_RUNTIME_COMPONENT_CAMERA_COMPONENT_H
#define KPENGINE_RUNTIME_COMPONENT_CAMERA_COMPONENT_H

#include "scene_component.h"
#include "math/math_header.h"
namespace kpengine
{


    struct CameraData{
        alignas(16) Matrix4f view;
        alignas(16) Matrix4f proj; 
    };

    class CameraComponent : public SceneComponent
    {
    public:
        CameraData GetCameraData() const;   
        Vector3f GetCameraUp() const{return up_;}
        Vector3f GetCameraForward() const {return forward_;}
        Vector3f GetCameraRight() const {return right_;}
        void SetRelativeRotation(const Rotatorf& new_rotator) override;
        void SetCameraAspect(float aspect);
        void SetCameraNearPlane(float near);
        void SetCameraFarPlane(float far);
    private:
        void Update();
        Matrix4f CalculateViewMatrix() const;
        Matrix4f CalculateProjectionMatrix() const;
    private:
        float fov_ = 45.f;
        float near_ = 0.1f;
        float far_ = 10.f;
        float aspect_ = {1920.f / 1080.f};
        Vector3f forward_;
        Vector3f up_;
        Vector3f right_;

        static Vector3f world_up;
    };
}

#endif