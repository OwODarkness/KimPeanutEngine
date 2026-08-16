#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "graphics/backend/common/api.h"

namespace kpengine::graphics
{
    class VulkanBackend;
}

namespace kpengine::render
{
    // The demo reappears here as the render module's first real scene: it owns the
    // mesh, texture, uniform buffers and descriptor sets, and records the frame's
    // draws through the RHI's public frame API. Vulkan-specific for now — draws are
    // recorded as raw vkCmd* against the backend's scene command buffer; a
    // cross-API scene abstraction is the reconstruction's next step
    // (docs/render/render_module.md).
    class RenderScene
    {
    public:
        RenderScene() = default;
        ~RenderScene() = default;

        void Initialize(graphics::VulkanBackend *backend);
        void Tick(float delta_time);
        void Record();
        void Cleanup();

    private:
        struct UniformBuffer
        {
            std::vector<graphics::BufferHandle> handles;
            std::vector<void *> mapped;
            uint32_t element_size = 0;
        };

        void CreateTexture();
        void CreateMesh();
        void CreateUniformBuffers();
        void CreateDescriptorSets();
        void WriteUniformBufferDescriptor(VkWriteDescriptorSet &out, VkDescriptorBufferInfo &info,
                                          VkDescriptorSet set, const UniformBuffer &ubo,
                                          VkDescriptorSetLayoutBinding binding, uint32_t frame_index);
        void WriteImageDescriptor(VkWriteDescriptorSet &out, VkDescriptorImageInfo &info,
                                  VkDescriptorSet set, VkDescriptorSetLayoutBinding binding,
                                  uint32_t frame_index);
        void UpdateUniformBuffers(uint32_t frame_index);

    private:
        graphics::VulkanBackend *backend_ = nullptr;
        graphics::TextureHandle texture_handle_;
        graphics::SamplerHandle sampler_handle_;
        graphics::MeshHandle mesh_handle_;
        UniformBuffer per_pass_ubo_;
        UniformBuffer per_object_ubo_;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptor_sets_;
    };
}

#endif
