#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_H

#include <glm/glm.hpp>
#include "runtime/core/math/vector3.h"
namespace kpengine
{
    using Vector3f = math::Vector3<float>;

    struct alignas(16) PointLight
    {
        Vector3f position{3.f, 3.f, 3.f}; // offset: 0
        float pad1;                        // padding to align next member
        Vector3f color{1.f, 1.f, 1.f};    // offset: 16
        float pad2;
        Vector3f ambient{0.3f, 0.3f, 0.3f}; // offset: 32
        float pad3;
        Vector3f diffuse{1.f, 1.f, 1.f}; // offset: 48
        float pad4;
        Vector3f specular{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
        float constant{1.f};     // offset: 80
        float linear{0.09f};     // offset: 84
        float quadratic{0.032f}; // offset: 88
        float pad6;              // padding to align structure size to multiple of 16
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
        float constant{1.f};     // offset: 96
        float linear{0.09f};       // offset: 100
        float quadratic{0.032f};    // offset: 104
        float cutoff;       // offset: 108
        float outer_cutoff; // offset: 112
        float pad7;         // padding to align structure size to multiple of 16
    };

    struct alignas(16) Light {
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