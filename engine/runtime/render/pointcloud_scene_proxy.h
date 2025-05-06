#ifndef KPENGINE_RUNTIME_POINTCLOUD_SCENE_PROXY_H
#define KPENGINE_RUNTIME_POINTCLOUD_SCENE_PROXY_H

#include "primitive_scene_proxy.h"

#define SHADER_PARAM_MODEL_TRANSFORM "model"

namespace kpengine{
    class RenderPointCloudResource;

    class PointCloudSceneProxy: public PrimitiveSceneProxy{
    public:
        void Draw(std::shared_ptr<RenderShader> shader) override;
        void Initialize() override;

    public:
        unsigned int vao_;
        RenderPointCloudResource* pointcloud_resource_ref_;
    };
}

#endif