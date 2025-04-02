#ifndef KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_H
#define KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_h

#include <memory>

namespace kpengine{
    class RenderShader;
    //render entity
    class PrimitiveSceneProxy{
        virtual void Draw(std::shared_ptr<RenderShader> shader) = 0;
    };
}

#endif