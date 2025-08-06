#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_H

#include "runtime/core/math/math_header.h"
namespace kpengine
{
    enum class LightType : int
    {
        None,
        Directional = 1,
        Point = 2,
        Spot = 3
    };

    struct alignas(16) GPULightData
    {
        int type;
        float padding0[3];

        Vector3f color;
        float intensity;

        Vector3f position;
        float padding1;

        Vector3f direction;
        float padding2;

        alignas(4) float radius;
        alignas(4) float inner_cutoff;
        alignas(4) float outer_cutoff;
        float padding3;
    };

    struct LightData
    {
        LightType light_type;
        Vector3f color;
        float intensity;
        LightData(LightType in_light_type, const Vector3f &in_color, float in_intensity) : light_type(in_light_type), color(in_color), intensity(1.f) {}
        virtual GPULightData ToGPULightData() const = 0;
    };

    struct DirectionalLightData : public LightData
    {

        Vector3f direction;

        DirectionalLightData() : LightData(LightType::Directional, {1.f, 1.f, 1.f}, 1.f),
                                 direction({-0.2f, -1.0f, -0.3f}) {}
        DirectionalLightData(const Vector3f &in_direction, const Vector3f &in_color, float in_intensity) : LightData(LightType::Directional, in_color, in_intensity),
                                                                                                           direction(in_direction) {}

        GPULightData ToGPULightData() const override
        {
            GPULightData data{};
            data.type = static_cast<int>(light_type);
            data.color = color;
            data.intensity = intensity;

            data.direction = direction;
            return data;
        }
    };

    struct PointLightData : LightData
    {
        Vector3f position;
        float radius;

        PointLightData() : LightData(LightType::Point, {1.f, 1.f, 1.f}, 1.f),
                           position({3.f, 3.f, 3.f}),
                           radius(10.f) {}
        PointLightData(const Vector3f &in_position, const Vector3f &in_color, float in_intensity, float in_radius) :
         LightData(LightType::Directional, in_color, in_intensity),
                                                                                                                     
         position(in_position),
                                                                                                                     radius(in_radius) {}

        GPULightData ToGPULightData() const override
        {
            GPULightData data{};

            data.type = static_cast<int>(light_type);
            data.color = color;
            data.intensity = intensity;

            data.position = position;
            data.radius = radius;
            return data;
        }
    };

    struct SpotLightData : LightData
    {
        Vector3f position;
        Vector3f direction;
        float radius;
        float inner_cutoff;
        float outer_cutoff;

        SpotLightData() : LightData(LightType::Spot, {1.f, 1.f, 1.f}, 1.f),
                          position({3.f, 3.f, 3.f}),
                          direction({-0.2f, -1.0f, -0.3f}),
                          radius(10.f),
                          inner_cutoff(math::DegreeToRadian(12.5f)), outer_cutoff(math::DegreeToRadian(17.5f)) {}
        SpotLightData(const Vector3f &in_position, const Vector3f &in_direction, const Vector3f &in_color, float in_intensity, float in_radius, float in_inner_cutoff, float in_outer_cutoff) : LightData(LightType::Spot, in_color, in_intensity),
                                                                                                                                                                                                position(in_position),
                                                                                                                                                                                                direction(in_direction),
                                                                                                                                                                                                radius(in_radius),
                                                                                                                                                                                                inner_cutoff(in_inner_cutoff), outer_cutoff(in_outer_cutoff) {}

        GPULightData ToGPULightData() const override
        {
            GPULightData data{};

            data.type = static_cast<int>(light_type);
            data.color = color;
            data.intensity = intensity;

            data.position = position;
            data.direction = direction;
            data.radius = radius;
            data.inner_cutoff = inner_cutoff;
            data.outer_cutoff = outer_cutoff;
            return data;
        }
    };

    struct alignas(16) PointLight
    {
        Vector3f position{3.f, 3.f, 3.f}; // offset: 0
        float pad1;                       // padding to align next member
        Vector3f color{1.f, 1.f, 1.f};    // offset: 16
        float pad2;
        Vector3f ambient{0.3f, 0.3f, 0.3f}; // offset: 32
        float pad3;
        Vector3f diffuse{1.f, 1.f, 1.f}; // offset: 48
        float pad4;
        Vector3f specular{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
    };

    struct alignas(16) DirectionalLight
    {
        Vector3f direction{-0.2f, -1.0f, -0.3f}; // offset: 0
        float pad1;
        Vector3f color{1.f, 1.f, 1.f}; // offset: 16
        float pad2;
        Vector3f ambient{0.3f, 0.3f, 0.3f}; // offset: 32
        float pad3;
        Vector3f diffuse{1.f, 1.f, 1.f}; // offset: 48
        float pad4;
        Vector3f specular{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
    };

    struct alignas(16) SpotLight
    {
        Vector3f position{0.f, 0.f, 0.f}; // offset: 0
        float pad1;
        Vector3f direction{-0.2f, -1.0f, -0.3f}; // offset: 16
        float pad2;
        Vector3f color{1.f, 1.f, 1.f}; // offset: 32
        float pad3;
        Vector3f ambient{0.3f, 0.3f, 0.3f}; // offset: 48
        float pad4;
        Vector3f diffuse{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
        Vector3f specular{1.f, 1.f, 1.f}; // offset: 80
        float pad6;

        float cutoff;       // offset: 108
        float outer_cutoff; // offset: 112
        float pad7;         // padding to align structure size to multiple of 16
    };

    struct alignas(16) Light
    {
        PointLight point_light;
        DirectionalLight directional_light;
        SpotLight spot_light;
    };

    struct AmbientLight
    {
        Vector3f ambient{0.3f, 0.3f, 0.3f};
    };

}

#endif