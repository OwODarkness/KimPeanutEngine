#ifndef KPENGINE_RUNTIME_MESH_SCENE_PROXY_H
#define KPENGINE_RUNTIME_MESH_SCENE_PROXY_H

#include "primitive_scene_proxy.h"

namespace kpengine{
    class MeshSceneProxy: public PrimitiveSceneProxy{
    public:
        virtual void Draw(std::shared_ptr<RenderShader> shader) override;
    };
}

#endif //KPENGINE_RUNTIME_MESH_SCENE_PROXY_H