#include "mesh_component.h"

#include "runtime/runtime_header.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/render_mesh.h"
#include "runtime/render/mesh_scene_proxy.h"

namespace kpengine{
    MeshComponent::MeshComponent(const std::string& mesh_realtive_path):
    mesh_(std::make_shared<RenderMesh_V2>(mesh_realtive_path))
    {

    }

    void MeshComponent::Initialize()
    {
        PrimitiveComponent::Initialize();
        mesh_->Initialize();
        RegisterSceneProxy();
    }

    void MeshComponent::RegisterSceneProxy()
    {
        scene_proxy_ = std::make_shared<MeshSceneProxy>();
        proxy_handle_ = runtime::global_runtime_context.render_system_->GetRenderScene()->AddProxy(scene_proxy_);
    }

    void MeshComponent::UnRegisterSceneProxy()
    {
        runtime::global_runtime_context.render_system_->GetRenderScene()->RemoveProxy(proxy_handle_);
        //unregister sceneproxy from renderscene
    }

    MeshComponent::~MeshComponent()
    {
        UnRegisterSceneProxy();
    }
}