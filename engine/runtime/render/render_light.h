#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_H

#include <glm/glm.hpp>

namespace kpengine
{

    struct PointLight
    {
        glm::vec3 position{3.f, 3.f, 3.f};
        glm::vec3 color{1.f, 1.f, 1.f};

        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
        glm::vec3 diffuse{1.f, 1.f, 1.f};
        glm::vec3 specular{1.f, 1.f, 1.f};

        float constant{1.f};
        float linear{0.09f};
        float quadratic{0.032f};
    };

    struct DirectionalLight
    {
        glm::vec3 direction{-0.2f, -1.0f, -0.3f};

        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
        glm::vec3 diffuse{1.f, 1.f, 1.f};
        glm::vec3 specular{1.f, 1.f, 1.f};
        glm::vec3 color{1.f, 1.f, 1.f};
    };



    struct SpotLight{
        glm::vec3 position{0.f, 0.f, 0.f};
        glm::vec3 direction{-0.2f, -1.0f, -0.3f};
        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
        glm::vec3 diffuse{1.f, 1.f, 1.f};
        glm::vec3 specular{1.f, 1.f, 1.f};
        glm::vec3 color{1.f, 1.f, 1.f};

        float constant{1.f};
        float linear{0.09f};
        float quadratic{0.032f};
        float cutoff;
        float outer_cutoff;
    };

    struct AmbientLight
    {
        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
    };

}

#endif