#ifndef KPENGINE_RUNTIME_PRIMITIVE_COMPONENT_H
#define KPENGINE_RUNTIME_PRIMITIVE_COMPONENT_H

#include <memory>
#include "scene_component.h"
#include "render/scene_proxy_handle.h"

namespace kpengine{
    class PrimitiveSceneProxy;

    class PrimitiveComponent: public SceneComponent{
    public:
        ~PrimitiveComponent() override;
        virtual void Initialize() override;
        virtual void TickComponent(float delta_time);
    protected:
        virtual void RegisterSceneProxy() = 0 ;
        virtual void UnRegisterSceneProxy() = 0;
    protected:
        std::shared_ptr<PrimitiveSceneProxy> scene_proxy_;    
        SceneProxyHandle proxy_handle_;
    };
}

#endif //KPENGINE_RUNTIME_PRIMITIVE_COMPONENT_H