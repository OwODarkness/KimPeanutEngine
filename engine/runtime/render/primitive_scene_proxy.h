#ifndef KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_H
#define KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_h

#include <memory>

#include "runtime/core/math/math_header.h"
#include "aabb.h"

namespace kpengine{
    class RenderShader;
    //render entity
    class PrimitiveSceneProxy{
    public:
        virtual void Draw(std::shared_ptr<RenderShader> shader) = 0;
        void UpdateTransform(const Transform3f& new_transform);
        void UpdateViewPosition(float* view_pos);
        void UpdateLightSpace(float* light_space);
        virtual void Initialize();
        Transform3f GetTransform() const{return transfrom_;}
        virtual AABB GetAABB();
    protected:
        PrimitiveSceneProxy(Transform3f transform = Transform3f());
    protected:
        Transform3f transfrom_;
        float* view_pos_;
        float* light_space_;
    public:
        unsigned int irradiance_map_handle_ = 0;
        unsigned int prefilter_map_handle_ = 0;
        unsigned int brdf_map_handle_ = 0;
        bool visible_this_frame = false;
    };
}

#endif