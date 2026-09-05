#include "gameplay/component/camera_component.h"

#include <cmath>

#include "gameplay/actor/actor.h"
#include "gameplay/scene_transform_utils.h"

namespace kpengine::gameplay
{
    namespace
    {
        constexpr float kMinimumFieldOfView = 1.0f;
        constexpr float kMaximumFieldOfView = 179.0f;
        constexpr float kMinimumPlane = 0.0001f;
        constexpr float kMinimumOrthographicHeight = 0.0001f;
        const Vector3f kWorldUp{0.0f, 1.0f, 0.0f};
        const Vector3f kFallbackRight{0.0f, 0.0f, -1.0f};
    }

    void CameraComponent::SetFieldOfView(float field_of_view_degrees)
    {
        if (std::isfinite(field_of_view_degrees) &&
            field_of_view_degrees >= kMinimumFieldOfView &&
            field_of_view_degrees <= kMaximumFieldOfView &&
            field_of_view_degrees_ != field_of_view_degrees)
        {
            field_of_view_degrees_ = field_of_view_degrees;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetNearPlane(float near_plane)
    {
        if (std::isfinite(near_plane) && near_plane >= kMinimumPlane &&
            near_plane < far_plane_ && near_plane_ != near_plane)
        {
            near_plane_ = near_plane;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetFarPlane(float far_plane)
    {
        if (std::isfinite(far_plane) && far_plane > near_plane_ && far_plane_ != far_plane)
        {
            far_plane_ = far_plane;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetOrthographicHeight(float orthographic_height)
    {
        if (std::isfinite(orthographic_height) &&
            orthographic_height >= kMinimumOrthographicHeight &&
            orthographic_height_ != orthographic_height)
        {
            orthographic_height_ = orthographic_height;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetProjectionMode(render::CameraProjectionMode projection_mode)
    {
        if ((projection_mode == render::CameraProjectionMode::Perspective ||
             projection_mode == render::CameraProjectionMode::Orthographic) &&
            projection_mode_ != projection_mode)
        {
            projection_mode_ = projection_mode;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetCameraEnabled(bool enabled)
    {
        if (enabled_ != enabled)
        {
            enabled_ = enabled;
            MarkSourceDirty();
        }
    }

    void CameraComponent::SetPriority(int priority)
    {
        if (priority_ != priority)
        {
            priority_ = priority;
            MarkSourceDirty();
        }
    }

    void CameraComponent::OnInitialize()
    {
        SceneComponent::OnInitialize();
        UpdateBasis();
    }

    void CameraComponent::OnActivate()
    {
        SceneComponent::OnActivate();
        UpdateBasis();

        Actor *const owner = GetOwner();
        render::ICameraSourceSink *const source_sink =
            owner != nullptr ? owner->GetCameraSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            source_handle_ = source_sink->EnqueueCreate(BuildSourceDesc());
        }
        source_dirty_ = false;
    }

    void CameraComponent::OnDeactivate()
    {
        Actor *const owner = GetOwner();
        render::ICameraSourceSink *const source_sink =
            owner != nullptr ? owner->GetCameraSourceSink() : nullptr;
        if (source_sink != nullptr && source_handle_.IsValid())
        {
            (void)source_sink->EnqueueDestroy(source_handle_);
        }
        source_handle_ = {};
        source_dirty_ = true;
        SceneComponent::OnDeactivate();
    }

    void CameraComponent::OnTick(float delta_time)
    {
        SceneComponent::OnTick(delta_time);
        FlushSourceUpdate();
    }

    void CameraComponent::OnTransformChanged()
    {
        UpdateBasis();
        MarkSourceDirty();
    }

    render::CameraSourceDesc CameraComponent::BuildSourceDesc() const
    {
        render::CameraSourceDesc source{};
        source.world_transform = GetWorldTransform();
        source.projection_mode = projection_mode_;
        source.field_of_view_degrees = field_of_view_degrees_;
        source.near_plane = near_plane_;
        source.far_plane = far_plane_;
        source.orthographic_height = orthographic_height_;
        source.enabled = enabled_;
        source.priority = priority_;
        return source;
    }

    void CameraComponent::UpdateBasis()
    {
        const Rotatorf rotation = GetWorldTransform().rotator_;
        forward_ = GetSceneForwardDirection(rotation);
        right_ = forward_.CrossProduct(kWorldUp);
        if (right_.SquareLength() <= kMinimumPlane * kMinimumPlane)
        {
            right_ = kFallbackRight;
        }
        else
        {
            right_ = right_.GetSafetyNormalize();
        }
        up_ = right_.CrossProduct(forward_).GetSafetyNormalize();
    }

    void CameraComponent::MarkSourceDirty()
    {
        source_dirty_ = true;
    }

    void CameraComponent::FlushSourceUpdate()
    {
        if (!source_dirty_ || !source_handle_.IsValid())
        {
            return;
        }

        Actor *const owner = GetOwner();
        render::ICameraSourceSink *const source_sink =
            owner != nullptr ? owner->GetCameraSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            (void)source_sink->EnqueueUpdate(source_handle_, BuildSourceDesc());
        }
        source_dirty_ = false;
    }
}
