#include "mesh_component.h"

#include "runtime/runtime_header.h"
#include "runtime/render/render_scene.h"
#include "runtime/render/render_mesh.h"
#include "runtime/render/mesh_scene_proxy.h"
#include "runtime/render/render_mesh_resource.h"
#include "runtime/render/render_camera.h"
#include "runtime/core/log/logger.h"

namespace kpengine{
    MeshComponent::MeshComponent():mesh_(nullptr){}
    MeshComponent::MeshComponent(const std::string& mesh_realtive_path):
    mesh_(std::make_shared<RenderMesh>(mesh_realtive_path))
    {

    }

    void MeshComponent::TickComponent(float delta_time)
    {
        PrimitiveComponent::TickComponent(delta_time);

        const Vector3f& camera_pos = runtime::global_runtime_context.render_system_->GetRenderCamera()->GetPosition();
        mesh_->UpdateLOD(camera_pos, Matrix4f::MakeTransformMatrix(GetWorldTransform()));
    }
    
    void MeshComponent::SetMesh(std::shared_ptr<RenderMesh> mesh)
    {
        mesh_ = mesh;
    }

    void MeshComponent::Initialize()
    {
        assert(mesh_);
        mesh_->Initialize();
        scene_proxy_ = std::make_shared<MeshSceneProxy>();
        MeshSceneProxy* mesh_proxy = dynamic_cast<MeshSceneProxy*>(scene_proxy_.get());
        if(mesh_proxy)
        {
            mesh_proxy->geometry_buffer_ = &mesh_->geometry_buf_;
            mesh_proxy->mesh_resourece_ref_ = mesh_->GetMeshResource();
            mesh_proxy->Initialize();
        }
        RegisterSceneProxy();
        PrimitiveComponent::Initialize();
    }

    void MeshComponent::RegisterSceneProxy()
    {

        proxy_handle_ = runtime::global_runtime_context.render_system_->GetRenderScene()->AddProxy(scene_proxy_);
        if(proxy_handle_.IsValid())
        {
            KP_LOG("MeshLog", LOG_LEVEL_DISPLAY, "proxy %s register succeed", mesh_->GetName().c_str());
        }
        else
        {
            KP_LOG("MeshLog", LOG_LEVEL_ERROR, "proxy %s register failed", mesh_->GetName().c_str());
        }
    }

    void MeshComponent::UnRegisterSceneProxy()
    {
        //unregister sceneproxy from renderscene
        runtime::global_runtime_context.render_system_->GetRenderScene()->RemoveProxy(proxy_handle_);
    }

    MeshComponent::~MeshComponent()
    {
        UnRegisterSceneProxy();
        mesh_.reset();        
    }
}