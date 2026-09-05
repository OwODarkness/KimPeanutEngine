#include "gameplay/reflection/gameplay_reflection_internal.h"

#include <cmath>
#include <limits>

#include "gameplay/component/directional_light_component.h"
#include "gameplay/component/point_light_component.h"
#include "gameplay/component/spot_light_component.h"

namespace kpengine::gameplay::reflection_detail
{
    namespace
    {
        constexpr float kHalfPi = 1.5707963267948966f;
        const float kConeMaximum = std::nextafter(kHalfPi, 0.0f);
        constexpr float kMinimumLightRange = std::numeric_limits<float>::denorm_min();

        bool IsFiniteNonNegative(const Vector3f &value) noexcept
        {
            return IsFinite(value) && value.x_ >= 0.0f && value.y_ >= 0.0f && value.z_ >= 0.0f;
        }

        template <typename T>
        float GetColorR(const T &component) { return component.GetColor().x_; }
        template <typename T>
        float GetColorG(const T &component) { return component.GetColor().y_; }
        template <typename T>
        float GetColorB(const T &component) { return component.GetColor().z_; }

        template <typename T>
        bool SetColorR(T &component, float value)
        {
            Vector3f color = component.GetColor();
            color.x_ = value;
            if (!IsFiniteNonNegative(color)) return false;
            component.SetColor(color);
            return true;
        }
        template <typename T>
        bool SetColorG(T &component, float value)
        {
            Vector3f color = component.GetColor();
            color.y_ = value;
            if (!IsFiniteNonNegative(color)) return false;
            component.SetColor(color);
            return true;
        }
        template <typename T>
        bool SetColorB(T &component, float value)
        {
            Vector3f color = component.GetColor();
            color.z_ = value;
            if (!IsFiniteNonNegative(color)) return false;
            component.SetColor(color);
            return true;
        }

        template <typename T>
        float GetIntensity(const T &component) { return component.GetIntensity(); }
        template <typename T>
        bool SetIntensity(T &component, float value)
        {
            if (!std::isfinite(value) || value < 0.0f) return false;
            component.SetIntensity(value);
            return true;
        }
        template <typename T>
        bool GetEnabled(const T &component) { return component.IsLightEnabled(); }
        template <typename T>
        void SetEnabled(T &component, bool value) { component.SetLightEnabled(value); }
        template <typename T>
        bool GetCastsShadow(const T &component) { return component.CastsShadow(); }
        template <typename T>
        void SetCastsShadow(T &component, bool value) { component.SetCastsShadow(value); }
        template <typename T>
        float GetRange(const T &component) { return component.GetRange(); }
        template <typename T>
        bool SetRange(T &component, float value)
        {
            if (!std::isfinite(value) || value <= 0.0f) return false;
            component.SetRange(value);
            return true;
        }

        float GetInnerCone(const SpotLightComponent &component)
        {
            return component.GetInnerConeRadians();
        }
        float GetOuterCone(const SpotLightComponent &component)
        {
            return component.GetOuterConeRadians();
        }
        bool IsValidCones(float inner, float outer) noexcept
        {
            return std::isfinite(inner) && std::isfinite(outer) && inner >= 0.0f &&
                   inner <= outer && outer < kHalfPi;
        }
        bool SetInnerCone(SpotLightComponent &component, float value)
        {
            if (!IsValidCones(value, component.GetOuterConeRadians())) return false;
            component.SetInnerConeRadians(value);
            return true;
        }
        bool SetOuterCone(SpotLightComponent &component, float value)
        {
            if (!IsValidCones(component.GetInnerConeRadians(), value)) return false;
            component.SetOuterConeRadians(value);
            return true;
        }

        template <typename T, bool HasRange, bool HasCones>
        ReflectionResult RegisterLightProperties(
            reflection::EnttReflectionTypeRegistrar<T> &type)
        {
            #define KP_REFLECT_TRY(...) \
                do { const ReflectionResult result = (__VA_ARGS__); if (!result) return result; } while (false)
            KP_REFLECT_TRY(type.template Property<&SetColorR<T>, &GetColorR<T>>(
                "light.color.r", kEditable,
                Metadata("Color R", "Light", ReflectionWidgetSemantic::Color, 0.0, {}, 0.01)));
            KP_REFLECT_TRY(type.template Property<&SetColorG<T>, &GetColorG<T>>(
                "light.color.g", kEditable,
                Metadata("Color G", "Light", ReflectionWidgetSemantic::Color, 0.0, {}, 0.01)));
            KP_REFLECT_TRY(type.template Property<&SetColorB<T>, &GetColorB<T>>(
                "light.color.b", kEditable,
                Metadata("Color B", "Light", ReflectionWidgetSemantic::Color, 0.0, {}, 0.01)));
            KP_REFLECT_TRY(type.template Property<&SetIntensity<T>, &GetIntensity<T>>(
                "light.intensity", kEditable,
                Metadata("Intensity", "Light", ReflectionWidgetSemantic::Default, 0.0, {}, 0.1)));
            KP_REFLECT_TRY(type.template Property<&SetEnabled<T>, &GetEnabled<T>>(
                "light.enabled", kEditable, Metadata("Enabled", "Light")));
            KP_REFLECT_TRY(type.template Property<&SetCastsShadow<T>, &GetCastsShadow<T>>(
                "light.casts_shadow", kEditable, Metadata("Casts Shadow", "Light")));
            if constexpr (HasRange)
            {
                KP_REFLECT_TRY(type.template Property<&SetRange<T>, &GetRange<T>>(
                    "light.range", kEditable,
                    Metadata("Range", "Light", ReflectionWidgetSemantic::Distance,
                             static_cast<double>(kMinimumLightRange), {}, 0.1)));
            }
            if constexpr (HasCones)
            {
                KP_REFLECT_TRY(type.template Property<&SetInnerCone, &GetInnerCone>(
                    "light.inner_cone", kEditable,
                    Metadata("Inner Cone", "Light", ReflectionWidgetSemantic::AngleRadians,
                             0.0, static_cast<double>(kConeMaximum), 0.01)));
                KP_REFLECT_TRY(type.template Property<&SetOuterCone, &GetOuterCone>(
                    "light.outer_cone", kEditable,
                    Metadata("Outer Cone", "Light", ReflectionWidgetSemantic::AngleRadians,
                             0.0, static_cast<double>(kConeMaximum), 0.01)));
            }
            #undef KP_REFLECT_TRY
            return {};
        }
    }

    ReflectionResult RegisterLightReflection(EnttReflectionRegistrar &registrar)
    {
        auto directional = registrar.Type<DirectionalLightComponent>(
            "kpengine.gameplay.DirectionalLightComponent");
        if (!directional) return directional.GetResult();
        ReflectionResult result = RegisterTransformChannels(directional, false, true, false);
        if (!result) return result;
        result = RegisterLightProperties<DirectionalLightComponent, false, false>(directional);
        if (!result) return result;

        auto point = registrar.Type<PointLightComponent>("kpengine.gameplay.PointLightComponent");
        if (!point) return point.GetResult();
        result = RegisterTransformChannels(point, true, false, false);
        if (!result) return result;
        result = RegisterLightProperties<PointLightComponent, true, false>(point);
        if (!result) return result;

        auto spot = registrar.Type<SpotLightComponent>("kpengine.gameplay.SpotLightComponent");
        if (!spot) return spot.GetResult();
        result = RegisterTransformChannels(spot, true, true, false);
        if (!result) return result;
        return RegisterLightProperties<SpotLightComponent, true, true>(spot);
    }
}
