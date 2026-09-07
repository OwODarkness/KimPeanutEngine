#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_DESCRIPTOR_SET_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_DESCRIPTOR_SET_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "common/resource_binding.h"
#include "common/profile_counters.h"

namespace kpengine::graphics
{
    class SamplerManager;
    class TextureManager;
    class VulkanBufferManager;
    struct VulkanPipelineResource;

    struct VulkanDescriptorSetResource
    {
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        uint32_t frame_slot = UINT32_MAX;
        std::size_t arena_index = 0;
        DescriptorSetHandle handle{};
    };

    struct VulkanDescriptorPoolArena
    {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        uint32_t max_sets = 0;
        uint32_t uniform_capacity = 0;
        uint32_t sampled_texture_capacity = 0;
        uint32_t used_sets = 0;
        uint32_t used_uniform_descriptors = 0;
        uint32_t used_sampled_texture_descriptors = 0;
    };

    class VulkanDescriptorSetManager
    {
    public:
        void Initialize(uint32_t frame_slot_count);
        void BeginFrame(VkDevice logical_device, uint32_t frame_slot);
        void EndFrame() noexcept { frame_active_ = false; }
        DescriptorSetHandle CreateResourceBindingSet(
            VkDevice logical_device, const VulkanPipelineResource &pipeline, const ResourceBindingSetDesc &desc,
            VulkanBufferManager &buffers, TextureManager &textures, SamplerManager &samplers,
            bool *pool_created = nullptr);
        bool DestroyResourceBindingSet(VkDevice logical_device, DescriptorSetHandle handle);
        void DestroyAll(VkDevice logical_device);
        VkDescriptorSet GetDescriptorSet(DescriptorSetHandle handle);
        DescriptorProfileCounters GetProfileCounters() const { return profile_counters_; }

    private:
        VulkanDescriptorPoolArena &CreateArena(
            VkDevice logical_device, std::vector<VulkanDescriptorPoolArena> &arenas,
            uint32_t required_sets, uint32_t required_uniform_descriptors,
            uint32_t required_sampled_texture_descriptors);

        std::vector<std::vector<VulkanDescriptorPoolArena>> frame_arenas_;
        std::vector<VulkanDescriptorPoolArena> persistent_arenas_;
        std::vector<VulkanDescriptorSetResource> resources_;
        HandleSystem<DescriptorSetHandle> handle_system_;
        uint32_t current_frame_slot_ = 0;
        bool frame_active_ = false;
        DescriptorProfileCounters profile_counters_{};
    };
}

#endif
