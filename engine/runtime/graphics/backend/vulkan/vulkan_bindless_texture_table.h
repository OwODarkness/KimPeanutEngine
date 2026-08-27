#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BINDLESS_TEXTURE_TABLE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BINDLESS_TEXTURE_TABLE_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "common/bindless_texture.h"
#include "common/api.h"

namespace kpengine::graphics
{
    class SamplerManager;
    class TextureManager;

    // Vulkan-private descriptor state for the common sampled-texture table.
    // Each frame slot receives its own set so descriptors are only rewritten
    // after that slot's fence has completed.
    class VulkanBindlessTextureTable final
    {
    public:
        bool Initialize(VkDevice device, uint32_t capacity, uint32_t frame_count);
        void Destroy(VkDevice device);

        BindlessTextureHandle Acquire(TextureHandle texture, SamplerHandle sampler,
                                      TextureManager &textures, SamplerManager &samplers);
        bool Release(BindlessTextureHandle handle, BindlessSubmissionSerial retire_after);
        void PrepareFrame(VkDevice device, uint32_t frame_index,
                          BindlessSubmissionSerial completed_submission,
                          TextureManager &textures, SamplerManager &samplers);
        void CollectCompletedSubmissions(BindlessSubmissionSerial completed_submission);

        VkDescriptorSetLayout GetLayout() const { return layout_; }
        VkDescriptorSet GetDescriptorSet(uint32_t frame_index) const;
        bool ReferencesTexture(TextureHandle handle) const;
        bool ReferencesSampler(SamplerHandle handle) const;
        bool IsReady() const { return layout_ != VK_NULL_HANDLE; }

    private:
        struct Entry
        {
            TextureHandle texture{};
            SamplerHandle sampler{};
            uint32_t revision = 0;
            bool live = false;
        };
        struct RetiredReference
        {
            TextureHandle texture{};
            SamplerHandle sampler{};
            BindlessSubmissionSerial retire_after = 0;
        };

        void CollectCompleted(BindlessSubmissionSerial completed_submission);
        void ApplyLiveEntries(VkDevice device, uint32_t frame_index,
                              TextureManager &textures, SamplerManager &samplers);

        BindlessTextureSlotAllocator allocator_{0};
        std::vector<Entry> entries_;
        std::vector<RetiredReference> retired_references_;
        std::vector<VkDescriptorSet> descriptor_sets_;
        std::vector<std::vector<uint32_t>> applied_revisions_;
        VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
        VkDescriptorPool pool_ = VK_NULL_HANDLE;
    };
}

#endif
