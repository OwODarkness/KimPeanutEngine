#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_H

#include <glm/glm.hpp>

namespace kpengine{

    struct PointLight{
        glm::vec3 position{3.f, 3.f, 3.f};
        glm::vec3 color{1.f, 1.f, 1.f};
    };

    struct AmbientLight{
        glm::vec3 ambient{0.3f, 0.3f, 0.3f};
    };
    
}

#endif