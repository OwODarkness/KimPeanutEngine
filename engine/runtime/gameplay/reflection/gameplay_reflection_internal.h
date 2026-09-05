#ifndef KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_INTERNAL_H
#define KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_INTERNAL_H

#include <cmath>
#include <optional>

#include "gameplay/component/scene_component.h"
#include "reflection/entt/entt_reflection_registrar.h"

namespace kpengine::gameplay::reflection_detail
{
    using reflection::EnttReflectionRegistrar;
    using reflection::ReflectionPropertyFlags;
    using reflection::ReflectionPropertyMetadata;
    using reflection::ReflectionResult;
    using reflection::ReflectionWidgetSemantic;

    constexpr ReflectionPropertyFlags kEditable =
        ReflectionPropertyFlags::Readable |
        ReflectionPropertyFlags::Writable |
        ReflectionPropertyFlags::EditorVisible;

    inline ReflectionPropertyMetadata Metadata(
        const char *display_name,
        const char *category,
        ReflectionWidgetSemantic semantic = ReflectionWidgetSemantic::Default,
        std::optional<double> minimum = {},
        std::optional<double> maximum = {},
        std::optional<double> step = {})
    {
        ReflectionPropertyMetadata metadata;
        metadata.display_name = display_name;
        metadata.category = category;
        metadata.semantic = semantic;
        metadata.minimum = minimum;
        metadata.maximum = maximum;
        metadata.step = step;
        return metadata;
    }

    inline bool IsFinite(const Vector3f &value) noexcept
    {
        return std::isfinite(value.x_) && std::isfinite(value.y_) &&
               std::isfinite(value.z_);
    }

    inline bool IsFinite(const Rotatorf &value) noexcept
    {
        return std::isfinite(value.pitch_) && std::isfinite(value.yaw_) &&
               std::isfinite(value.roll_);
    }

    template <typename T>
    float GetLocationX(const T &component) { return component.GetLocalLocation().x_; }
    template <typename T>
    float GetLocationY(const T &component) { return component.GetLocalLocation().y_; }
    template <typename T>
    float GetLocationZ(const T &component) { return component.GetLocalLocation().z_; }

    template <typename T>
    bool SetLocationX(T &component, float value)
    {
        Vector3f location = component.GetLocalLocation();
        location.x_ = value;
        if (!IsFinite(location)) return false;
        component.SetLocalLocation(location);
        return true;
    }
    template <typename T>
    bool SetLocationY(T &component, float value)
    {
        Vector3f location = component.GetLocalLocation();
        location.y_ = value;
        if (!IsFinite(location)) return false;
        component.SetLocalLocation(location);
        return true;
    }
    template <typename T>
    bool SetLocationZ(T &component, float value)
    {
        Vector3f location = component.GetLocalLocation();
        location.z_ = value;
        if (!IsFinite(location)) return false;
        component.SetLocalLocation(location);
        return true;
    }

    template <typename T>
    float GetPitch(const T &component) { return component.GetLocalTransform().rotator_.pitch_; }
    template <typename T>
    float GetYaw(const T &component) { return component.GetLocalTransform().rotator_.yaw_; }
    template <typename T>
    float GetRoll(const T &component) { return component.GetLocalTransform().rotator_.roll_; }

    template <typename T>
    bool SetPitch(T &component, float value)
    {
        Rotatorf rotation = component.GetLocalTransform().rotator_;
        rotation.pitch_ = value;
        if (!IsFinite(rotation)) return false;
        component.SetLocalRotation(rotation);
        return true;
    }
    template <typename T>
    bool SetYaw(T &component, float value)
    {
        Rotatorf rotation = component.GetLocalTransform().rotator_;
        rotation.yaw_ = value;
        if (!IsFinite(rotation)) return false;
        component.SetLocalRotation(rotation);
        return true;
    }
    template <typename T>
    bool SetRoll(T &component, float value)
    {
        Rotatorf rotation = component.GetLocalTransform().rotator_;
        rotation.roll_ = value;
        if (!IsFinite(rotation)) return false;
        component.SetLocalRotation(rotation);
        return true;
    }

    template <typename T>
    float GetScaleX(const T &component) { return component.GetLocalTransform().scale_.x_; }
    template <typename T>
    float GetScaleY(const T &component) { return component.GetLocalTransform().scale_.y_; }
    template <typename T>
    float GetScaleZ(const T &component) { return component.GetLocalTransform().scale_.z_; }

    template <typename T>
    bool SetScaleX(T &component, float value)
    {
        Vector3f scale = component.GetLocalTransform().scale_;
        scale.x_ = value;
        if (!IsFinite(scale)) return false;
        component.SetLocalScale(scale);
        return true;
    }
    template <typename T>
    bool SetScaleY(T &component, float value)
    {
        Vector3f scale = component.GetLocalTransform().scale_;
        scale.y_ = value;
        if (!IsFinite(scale)) return false;
        component.SetLocalScale(scale);
        return true;
    }
    template <typename T>
    bool SetScaleZ(T &component, float value)
    {
        Vector3f scale = component.GetLocalTransform().scale_;
        scale.z_ = value;
        if (!IsFinite(scale)) return false;
        component.SetLocalScale(scale);
        return true;
    }

    template <typename T>
    ReflectionResult RegisterTransformChannels(
        reflection::EnttReflectionTypeRegistrar<T> &type,
        bool location,
        bool rotation,
        bool scale)
    {
        #define KP_REFLECT_TRY(...) \
            do { const ReflectionResult result = (__VA_ARGS__); if (!result) return result; } while (false)
        if (location)
        {
            KP_REFLECT_TRY(type.template Property<&SetLocationX<T>, &GetLocationX<T>>(
                "transform.location.x", kEditable,
                Metadata("Location X", "Transform", ReflectionWidgetSemantic::Position, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetLocationY<T>, &GetLocationY<T>>(
                "transform.location.y", kEditable,
                Metadata("Location Y", "Transform", ReflectionWidgetSemantic::Position, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetLocationZ<T>, &GetLocationZ<T>>(
                "transform.location.z", kEditable,
                Metadata("Location Z", "Transform", ReflectionWidgetSemantic::Position, {}, {}, 0.1)));
        }
        if (rotation)
        {
            KP_REFLECT_TRY(type.template Property<&SetPitch<T>, &GetPitch<T>>(
                "transform.rotation.pitch", kEditable,
                Metadata("Pitch", "Transform", ReflectionWidgetSemantic::RotationDegrees, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetYaw<T>, &GetYaw<T>>(
                "transform.rotation.yaw", kEditable,
                Metadata("Yaw", "Transform", ReflectionWidgetSemantic::RotationDegrees, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetRoll<T>, &GetRoll<T>>(
                "transform.rotation.roll", kEditable,
                Metadata("Roll", "Transform", ReflectionWidgetSemantic::RotationDegrees, {}, {}, 0.1)));
        }
        if (scale)
        {
            KP_REFLECT_TRY(type.template Property<&SetScaleX<T>, &GetScaleX<T>>(
                "transform.scale.x", kEditable,
                Metadata("Scale X", "Transform", ReflectionWidgetSemantic::Scale, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetScaleY<T>, &GetScaleY<T>>(
                "transform.scale.y", kEditable,
                Metadata("Scale Y", "Transform", ReflectionWidgetSemantic::Scale, {}, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetScaleZ<T>, &GetScaleZ<T>>(
                "transform.scale.z", kEditable,
                Metadata("Scale Z", "Transform", ReflectionWidgetSemantic::Scale, {}, {}, 0.1)));
        }
        #undef KP_REFLECT_TRY
        return {};
    }

    ReflectionResult RegisterActorReflection(EnttReflectionRegistrar &registrar);
    ReflectionResult RegisterLightReflection(EnttReflectionRegistrar &registrar);
    ReflectionResult RegisterCameraReflection(EnttReflectionRegistrar &registrar);
}

#endif
