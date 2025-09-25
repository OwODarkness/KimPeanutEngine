#include "mesh_manager.h"
#include "opengl/opengl_mesh.h"
#include "vulkan/vulkan_mesh.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    MeshHandle MeshManager::CreateMesh(const GraphicsContext &context, const MeshData &data)
    {
        MeshHandle handle = handle_system_.Create();
        
        if(handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        

        MeshSlot& resource = resources_[handle.id];
        if(context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            resource.mesh = std::make_unique<OpenglMesh>();
        }
        else if(context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            resource.mesh = std::make_unique<VulkanMesh>();
        }

        resource.mesh->Initialize(context, data);

        return handle;
    }
    MeshSlot *MeshManager::GetMeshSlot(MeshHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);
        if(index >= resources_.size())
        {
            KP_LOG("MeshManagerLog", LOG_LEVEL_ERROR, "Failed to get mesh by handle");
            throw std::runtime_error("Failed to get mesh by handle");
        }
        return &resources_[index];
    }
    Mesh *MeshManager::GetMesh(MeshHandle handle)
    {
        MeshSlot* resource = GetMeshSlot(handle);
        if(resource)
        {
            return resource->mesh.get();
        }
        return nullptr;
    }
    bool MeshManager::DestroyMesh(const GraphicsContext& context,  MeshHandle handle)
    {
        Mesh* mesh = GetMesh(handle);
        if(!mesh)
        {
            return false;
        }
        mesh->Destroy(context);
        return handle_system_.Destroy(handle);
    }
}