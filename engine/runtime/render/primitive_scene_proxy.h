#ifndef KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_H
#define KPENGINE_RUNTIME_PRIMITIVE_SCENE_PROXY_h

#include <memory>

#include "runtime/core/math/math_header.h"
#include "render_context.h"
#include "aabb.h"

namespace kpengine{
    class RenderShader;
    //render entity
    class PrimitiveSceneProxy{
    public:
        virtual void Draw(const RenderContext& context) const = 0;
        virtual void DrawGeometryPass(const RenderContext& context) const = 0;
        void UpdateTransform(const Transform3f& new_transform);
        void UpdateLightSpace(float* light_space);
        virtual void Initialize();
        Transform3f GetTransform() const{return transfrom_;}
        virtual AABB GetAABB();
        bool IsVisible() const{return visible_this_frame_;}
        void SetVisibility(bool visible);
    protected:
        PrimitiveSceneProxy(Transform3f transform = Transform3f());
    protected:
        Transform3f transfrom_;
        float* light_space_;
        bool visible_this_frame_ = false;
    public:
        int id_ = -1;
    };
}

#endif