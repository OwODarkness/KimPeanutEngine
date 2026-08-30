#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_CAMERA_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_CAMERA_COMPONENT_H

#include "gameplay/component/scene_component.h"
#include "render/camera_source.h"

namespace kpengine::gameplay
{
    // Gameplay-owned camera authoring state. It owns transform/lens values and
    // publishes only a copied camera source; it never owns render matrices.
    class CameraComponent final : public SceneComponent
    {
    public:
        float GetFieldOfView() const { return field_of_view_degrees_; }
        float GetNearPlane() const { return near_plane_; }
        float GetFarPlane() const { return far_plane_; }
        float GetOrthographicHeight() const { return orthographic_height_; }
        render::CameraProjectionMode GetProjectionMode() const { return projection_mode_; }
        bool IsCameraEnabled() const { return enabled_; }
        int GetPriority() const { return priority_; }

        const Vector3f &GetForward() const { return forward_; }
        const Vector3f &GetUp() const { return up_; }
        const Vector3f &GetRight() const { return right_; }
        render::CameraSourceHandle GetSourceHandle() const { return source_handle_; }

        // Invalid values are ignored and leave the last valid setting intact.
        void SetFieldOfView(float field_of_view_degrees);
        void SetNearPlane(float near_plane);
        void SetFarPlane(float far_plane);
        void SetOrthographicHeight(float orthographic_height);
        void SetProjectionMode(render::CameraProjectionMode projection_mode);
        void SetCameraEnabled(bool enabled);
        void SetPriority(int priority);

    protected:
        void OnInitialize() override;
        void OnActivate() override;
        void OnDeactivate() override;
        void OnTick(float delta_time) override;
        void OnTransformChanged() override;

    private:
        render::CameraSourceDesc BuildSourceDesc() const;
        void UpdateBasis();
        void MarkSourceDirty();
        void FlushSourceUpdate();

        render::CameraProjectionMode projection_mode_ =
            render::CameraProjectionMode::Perspective;
        float field_of_view_degrees_ = 45.0f;
        float near_plane_ = 0.1f;
        float far_plane_ = 2000.0f;
        float orthographic_height_ = 10.0f;
        bool enabled_ = true;
        int priority_ = 0;

        Vector3f forward_{1.0f, 0.0f, 0.0f};
        Vector3f up_{0.0f, 1.0f, 0.0f};
        Vector3f right_{0.0f, 0.0f, -1.0f};
        bool source_dirty_ = true;
        render::CameraSourceHandle source_handle_;
    };
}

#endif
