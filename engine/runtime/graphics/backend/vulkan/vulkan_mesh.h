#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MESH_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MESH_H

#include "common/mesh.h"
#include "common/api.h"
namespace kpengine::graphics{

    struct VulkanMeshResource{
        BufferHandle vertex_handle;
        BufferHandle index_handle;
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        std::vector<MeshSection> sections;
    };

    class VulkanMesh: public Mesh{
    public:
        MeshResource GetMeshHandle() const override;
        void Initialize(const GraphicsContext& context, const MeshData& data) override;
        void Destroy(const GraphicsContext& context);
    private:
        VulkanMeshResource resource_;
    };
}

#endif
