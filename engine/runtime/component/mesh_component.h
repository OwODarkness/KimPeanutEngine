#ifndef KPENGINE_RUNTIME_MESH_COMPONENT_H
#define KPENGINE_RUNTIME_MESH_COMPONENT_H

#include <memory>
#include <string>
#include "primitive_component.h"

namespace kpengine{
    class RenderMesh_V2;    

    class MeshComponent: public PrimitiveComponent{
    public:
        MeshComponent(const std::string& mesh_realtive_path);
        virtual void Initialize() override;

        virtual ~MeshComponent();
    protected:
        virtual void RegisterSceneProxy() override;
        virtual void UnRegisterSceneProxy() override;
    private:
        std::shared_ptr<RenderMesh_V2> mesh_;
    };
}

#endif