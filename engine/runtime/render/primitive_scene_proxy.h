#ifndef KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_H
#define KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_h

#include <memory>

#include "runtime/core/math/math_header.h"

namespace kpengine{
    class RenderShader;
    //render entity
    class PrimitiveSceneProxy{
    public:
        virtual void Draw(std::shared_ptr<RenderShader> shader) = 0;
        void UpdateTransform(const Transform3f& new_transform);
    protected:
        Transform3f transfrom_;
    };
}

#endif