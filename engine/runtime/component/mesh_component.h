#ifndef KPENGINE_RUNTIME_MESH_COMPONENT_H
#define KPENGINE_RUNTIME_MESH_COMPONENT_H

#include <memory>
#include <string>
#include "primitive_component.h"

namespace kpengine{
    class RenderMesh;    

    class MeshComponent: public PrimitiveComponent{
    public:
        MeshComponent();
        MeshComponent(const std::string& mesh_realtive_path);
        virtual void TickComponent(float delta_time) override;
        void SetMesh(std::shared_ptr<RenderMesh> mesh);
        std::shared_ptr<RenderMesh> GetMesh(){return mesh_;}
        virtual void Initialize() override;
        ~MeshComponent() override;
    protected:
        virtual void RegisterSceneProxy() override;
        virtual void UnRegisterSceneProxy() override;
    protected:
        std::shared_ptr<RenderMesh> mesh_;
    };
}

#endif