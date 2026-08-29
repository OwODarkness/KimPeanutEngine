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

    // A scene-owned view definition. Input and gameplay decide its transform;
    // the render scene only derives the per-frame view/projection data from it.
    class RenderCamera
    {
    public:
        void SetPosition(const Vector3f &position) { position_ = position; }
        void SetRotation(const Rotatorf &rotation) { rotation_ = rotation; Update(); }
        void SetAspect(float aspect) { aspect_ = aspect; }
        void SetFOV(float fov) { fov_ = fov; }
        void SetNearPlane(float near_plane) { near_ = near_plane; }
        void SetFarPlane(float far_plane) { far_ = far_plane; }

        const Vector3f &GetPosition() const { return position_; }
        const Rotatorf &GetRotation() const { return rotation_; }

        CameraData GetCameraData() const;
        // CPU-side visibility uses the math-library matrix convention directly.
        // GetCameraData() remains GPU-facing and transposes its matrices for UBO upload.
        Matrix4f GetViewProjectionMatrix() const;

    private:
        void Update();
        Matrix4f CalculateViewMatrix() const;
        Matrix4f CalculateProjectionMatrix() const;

    private:
        // Preserve the demo's existing framing while making it scene state.
        Vector3f position_{0.f, 0.f, 2.f};
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
