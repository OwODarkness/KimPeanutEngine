#ifndef KPENGINE_RUNTIME_SHADOW_CASTER_H
#define KPENGINE_RUNTIME_SHADOW_CASTER_H

#include <memory>
#include <vector>

#include "runtime/core/math/math_header.h"

namespace kpengine
{
    struct LightData;
    class PrimitiveSceneProxy;
    class RenderShader;

    class ShadowCaster
    {
    public:
        virtual void Initialize() = 0;
        virtual void AddLight(std::shared_ptr<LightData> light) = 0;
        virtual void Render(const std::vector<std::shared_ptr<PrimitiveSceneProxy>> &proxies) = 0;
        
        unsigned int GetShadowMap() const { return shadow_map_; }

        void SetShadowMapSize(int width, int height);
        void SetNearPlane(float near);
        void SetFarPlane(float far);

    protected:
        unsigned int fbo_;
        unsigned int shadow_map_;
        int width_ = 1024;
        int height_ = 1024;
        float near_plane_;
        float far_plane_ = 25.f;
        std::shared_ptr<RenderShader> shader_;
    };

}

#endif