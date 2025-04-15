#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_H

#include <glm/glm.hpp>

namespace kpengine
{
    struct alignas(16) PointLight
    {
        glm::vec3 position{3.f, 3.f, 3.f}; // offset: 0
        float pad1;                        // padding to align next member
        glm::vec3 color{1.f, 1.f, 1.f};    // offset: 16
        float pad2;
        glm::vec3 ambient{0.3f, 0.3f, 0.3f}; // offset: 32
        float pad3;
        glm::vec3 diffuse{1.f, 1.f, 1.f}; // offset: 48
        float pad4;
        glm::vec3 specular{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
        float constant{1.f};     // offset: 80
        float linear{0.09f};     // offset: 84
        float quadratic{0.032f}; // offset: 88
        float pad6;              // padding to align structure size to multiple of 16
    };

    struct alignas(16) DirectionalLight
    {
        glm::vec3 direction{-0.2f, -1.0f, -0.3f}; // offset: 0
        float pad1;
        glm::vec3 color{1.f, 1.f, 1.f}; // offset: 16
        float pad2;
        glm::vec3 ambient{0.3f, 0.3f, 0.3f}; // offset: 32
        float pad3;
        glm::vec3 diffuse{1.f, 1.f, 1.f}; // offset: 48
        float pad4;
        glm::vec3 specular{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
    };

    struct alignas(16) SpotLight
    {
        glm::vec3 position{0.f, 0.f, 0.f}; // offset: 0
        float pad1;
        glm::vec3 direction{-0.2f, -1.0f, -0.3f}; // offset: 16
        float pad2;
        glm::vec3 color{1.f, 1.f, 1.f}; // offset: 32
        float pad3;
        glm::vec3 ambient{0.3f, 0.3f, 0.3f}; // offset: 48
        float pad4;
        glm::vec3 diffuse{1.f, 1.f, 1.f}; // offset: 64
        float pad5;
        glm::vec3 specular{1.f, 1.f, 1.f}; // offset: 80
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
        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
    };

}

#endif