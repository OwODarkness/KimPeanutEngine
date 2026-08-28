#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_PRIMITIVE_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_PRIMITIVE_COMPONENT_H

#include "gameplay/component/scene_component.h"
#include "spatial/aabb.h"

namespace kpengine::gameplay
{
    class PrimitiveComponent : public SceneComponent
    {
    public:
        bool IsVisible() const { return visible_; }
        bool CastsShadow() const { return casts_shadow_; }
        const spatial::AABB &GetLocalBounds() const { return local_bounds_; }
        spatial::AABB GetWorldBounds() const;

        void SetVisible(bool visible);
        void SetCastsShadow(bool casts_shadow);
        void SetLocalBounds(const spatial::AABB &bounds);

    protected:
        virtual void OnPrimitiveStateChanged() {}

    private:
        spatial::AABB local_bounds_{};
        bool visible_ = true;
        bool casts_shadow_ = true;
    };
}

#endif
