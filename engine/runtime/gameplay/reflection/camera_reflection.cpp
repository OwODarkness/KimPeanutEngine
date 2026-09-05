#include "gameplay/reflection/gameplay_reflection_internal.h"

#include "gameplay/component/camera_component.h"
#include "render/camera_source.h"

namespace kpengine::gameplay::reflection_detail
{
    namespace
    {
        constexpr float kMinimumCameraPlane = 0.0001f;
        constexpr float kMinimumOrthographicHeight = 0.0001f;
        constexpr float kMaximumFieldOfView = 179.0f;

        ReflectionPropertyMetadata ProjectionMetadata()
        {
            ReflectionPropertyMetadata metadata = Metadata(
                "Projection", "Camera", ReflectionWidgetSemantic::Enum);
            metadata.enum_options = {{0, "Perspective"}, {1, "Orthographic"}};
            return metadata;
        }

        float GetFieldOfView(const CameraComponent &component) { return component.GetFieldOfView(); }
        bool SetFieldOfView(CameraComponent &component, float value)
        {
            if (!std::isfinite(value) || value < 1.0f || value > kMaximumFieldOfView) return false;
            component.SetFieldOfView(value);
            return true;
        }
        float GetNearPlane(const CameraComponent &component) { return component.GetNearPlane(); }
        bool SetNearPlane(CameraComponent &component, float value)
        {
            if (!std::isfinite(value) || value < kMinimumCameraPlane ||
                value >= component.GetFarPlane()) return false;
            component.SetNearPlane(value);
            return true;
        }
        float GetFarPlane(const CameraComponent &component) { return component.GetFarPlane(); }
        bool SetFarPlane(CameraComponent &component, float value)
        {
            if (!std::isfinite(value) || value <= component.GetNearPlane()) return false;
            component.SetFarPlane(value);
            return true;
        }
        float GetOrthographicHeight(const CameraComponent &component)
        {
            return component.GetOrthographicHeight();
        }
        bool SetOrthographicHeight(CameraComponent &component, float value)
        {
            if (!std::isfinite(value) || value < kMinimumOrthographicHeight) return false;
            component.SetOrthographicHeight(value);
            return true;
        }
        int GetProjection(const CameraComponent &component)
        {
            return component.GetProjectionMode() == render::CameraProjectionMode::Perspective ? 0 : 1;
        }
        bool SetProjection(CameraComponent &component, int value)
        {
            if (value == 0)
            {
                component.SetProjectionMode(render::CameraProjectionMode::Perspective);
                return true;
            }
            if (value == 1)
            {
                component.SetProjectionMode(render::CameraProjectionMode::Orthographic);
                return true;
            }
            return false;
        }
        bool GetCameraEnabled(const CameraComponent &component) { return component.IsCameraEnabled(); }
        void SetCameraEnabled(CameraComponent &component, bool value) { component.SetCameraEnabled(value); }
        int GetPriority(const CameraComponent &component) { return component.GetPriority(); }
        void SetPriority(CameraComponent &component, int value) { component.SetPriority(value); }
    }

    ReflectionResult RegisterCameraReflection(EnttReflectionRegistrar &registrar)
    {
        auto camera = registrar.Type<CameraComponent>("kpengine.gameplay.CameraComponent");
        if (!camera) return camera.GetResult();
        ReflectionResult result = RegisterTransformChannels(camera, true, true, false);
        if (!result) return result;

        #define KP_REFLECT_TRY(...) \
            do { const ReflectionResult property_result = (__VA_ARGS__); if (!property_result) return property_result; } while (false)
        KP_REFLECT_TRY(camera.Property<&SetProjection, &GetProjection>(
            "camera.projection", kEditable, ProjectionMetadata()));
        KP_REFLECT_TRY(camera.Property<&SetFieldOfView, &GetFieldOfView>(
            "camera.field_of_view", kEditable,
            Metadata("Field of View", "Camera", ReflectionWidgetSemantic::Default,
                     1.0, static_cast<double>(kMaximumFieldOfView), 0.1)));
        KP_REFLECT_TRY(camera.Property<&SetNearPlane, &GetNearPlane>(
            "camera.near_plane", kEditable,
            Metadata("Near Plane", "Camera", ReflectionWidgetSemantic::Distance,
                     static_cast<double>(kMinimumCameraPlane), {}, 0.01)));
        KP_REFLECT_TRY(camera.Property<&SetFarPlane, &GetFarPlane>(
            "camera.far_plane", kEditable,
            Metadata("Far Plane", "Camera", ReflectionWidgetSemantic::Distance,
                     static_cast<double>(kMinimumCameraPlane), {}, 0.1)));
        KP_REFLECT_TRY(camera.Property<&SetOrthographicHeight, &GetOrthographicHeight>(
            "camera.orthographic_height", kEditable,
            Metadata("Orthographic Height", "Camera", ReflectionWidgetSemantic::Distance,
                     static_cast<double>(kMinimumOrthographicHeight), {}, 0.1)));
        KP_REFLECT_TRY(camera.Property<&SetCameraEnabled, &GetCameraEnabled>(
            "camera.enabled", kEditable, Metadata("Enabled", "Camera")));
        KP_REFLECT_TRY(camera.Property<&SetPriority, &GetPriority>(
            "camera.priority", kEditable, Metadata("Priority", "Camera")));
        #undef KP_REFLECT_TRY
        return {};
    }
}
