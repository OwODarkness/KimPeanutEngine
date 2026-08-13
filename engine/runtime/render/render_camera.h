#ifndef KPENGINE_RUNTIME_RENDER_RENDER_CAMERA_H
#define KPENGINE_RUNTIME_RENDER_RENDER_CAMERA_H

#include "math/math_header.h"

namespace kpengine::render
{
    struct CameraData
    {
        alignas(16) Matrix4f view;
        alignas(16) Matrix4f proj;
    };

    // Temp camera: pure data holder, modeled on the deprecated CameraComponent
    // (git history: engine/runtime/component/camera_component.*). No input, no
    // movement, no scene hierarchy -- just the camera parameters and the
    // view/proj derivation the render pass needs.
    class RenderCamera
    {
    public:
        void SetPosition(const Vector3f &position) { position_ = position; }
        void SetRotation(const Rotatorf &rotation) { rotation_ = rotation; Update(); }
        void SetAspect(float aspect) { aspect_ = aspect; }
        void SetFOV(float fov) { fov_ = fov; }

        const Vector3f &GetPosition() const { return position_; }
        const Rotatorf &GetRotation() const { return rotation_; }

        CameraData GetCameraData() const;

    private:
        void Update();
        Matrix4f CalculateViewMatrix() const;
        Matrix4f CalculateProjectionMatrix() const;

    private:
        Vector3f position_{0.f, 0.f, 0.f};
        Rotatorf rotation_{0.f, -90.f, 0.f}; // yaw -90: look down -Z (old default)
        float fov_ = 45.f;
        float near_ = 0.1f;
        float far_ = 10.f;
        float aspect_ = 1920.f / 1080.f;

        Vector3f forward_{0.f, 0.f, -1.f};
        Vector3f up_{0.f, 1.f, 0.f};
        Vector3f right_{1.f, 0.f, 0.f};
        static Vector3f world_up;
    };
}

#endif
