#include "vulkan_bindless_texture_table.h"

#include <algorithm>

#include "common/sampler.h"
#include "common/sampler_manager.h"
#include "common/texture.h"
#include "common/texture_manager.h"
#include "vulkan_sampler.h"
#include "vulkan_texture.h"

namespace kpengine::graphics
{
    bool VulkanBindlessTextureTable::Initialize(VkDevice device, uint32_t capacity,
                                                uint32_t frame_count)
    {
        if (!IsBindlessTextureTableCapacityValid(capacity) || frame_count == 0)
        {
            return false;
        }

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = BindlessTextureTableLayout::descriptor_binding;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = capacity;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        const VkDescriptorBindingFlags binding_flags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
        flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flags_info.bindingCount = 1;
        flags_info.pBindingFlags = &binding_flags;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.pNext = &flags_info;
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout_) != VK_SUCCESS)
        {
            layout_ = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = capacity * frame_count;
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool_info.maxSets = frame_count;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool_) != VK_SUCCESS)
        {
            vkDestroyDescriptorSetLayout(device, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
            return false;
        }

        std::vector<VkDescriptorSetLayout> layouts(frame_count, layout_);
        descriptor_sets_.resize(frame_count);
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = pool_;
        allocate_info.descriptorSetCount = frame_count;
        allocate_info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocate_info, descriptor_sets_.data()) != VK_SUCCESS)
        {
            Destroy(device);
            return false;
        }

        allocator_ = BindlessTextureSlotAllocator{capacity};
        entries_.resize(capacity);
        applied_revisions_.assign(frame_count, std::vector<uint32_t>(capacity, 0));
        return true;
    }

    void VulkanBindlessTextureTable::Destroy(VkDevice device)
    {
        if (pool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
        if (layout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
        descriptor_sets_.clear();
        applied_revisions_.clear();
        entries_.clear();
        retired_references_.clear();
        allocator_ = BindlessTextureSlotAllocator{0};
    }

    BindlessTextureHandle VulkanBindlessTextureTable::Acquire(
        TextureHandle texture, SamplerHandle sampler, TextureManager &textures,
        SamplerManager &samplers)
    {
        if (!IsReady() || !textures.GetTexture(texture) || !samplers.GetSampler(sampler))
        {
            return {};
        }
        const BindlessTextureHandle handle = allocator_.Allocate();
        if (!handle.IsValid())
        {
            return {};
        }
        Entry &entry = entries_[handle.id];
        entry.texture = texture;
        entry.sampler = sampler;
        entry.live = true;
        ++entry.revision;
        if (entry.revision == 0)
        {
            ++entry.revision;
        }
        return handle;
    }

    bool VulkanBindlessTextureTable::Release(BindlessTextureHandle handle,
                                             BindlessSubmissionSerial retire_after)
    {
        if (!allocator_.IsAllocated(handle))
        {
            return false;
        }
        Entry &entry = entries_[handle.id];
        retired_references_.push_back({entry.texture, entry.sampler, retire_after});
        entry.live = false;
        return allocator_.Release(handle, retire_after);
    }

    void VulkanBindlessTextureTable::PrepareFrame(
        VkDevice device, uint32_t frame_index, BindlessSubmissionSerial completed_submission,
        TextureManager &textures, SamplerManager &samplers)
    {
        CollectCompleted(completed_submission);
        if (frame_index >= descriptor_sets_.size())
        {
            return;
        }
        ApplyLiveEntries(device, frame_index, textures, samplers);
    }

    void VulkanBindlessTextureTable::CollectCompletedSubmissions(
        BindlessSubmissionSerial completed_submission)
    {
        CollectCompleted(completed_submission);
    }

    VkDescriptorSet VulkanBindlessTextureTable::GetDescriptorSet(uint32_t frame_index) const
    {
        return frame_index < descriptor_sets_.size() ? descriptor_sets_[frame_index] : VK_NULL_HANDLE;
    }

    bool VulkanBindlessTextureTable::ReferencesTexture(TextureHandle handle) const
    {
        return std::any_of(entries_.begin(), entries_.end(), [handle](const Entry &entry) {
                   return entry.live && entry.texture == handle;
               }) ||
               std::any_of(retired_references_.begin(), retired_references_.end(), [handle](const RetiredReference &entry) {
                   return entry.texture == handle;
               });
    }

    bool VulkanBindlessTextureTable::ReferencesSampler(SamplerHandle handle) const
    {
        return std::any_of(entries_.begin(), entries_.end(), [handle](const Entry &entry) {
                   return entry.live && entry.sampler == handle;
               }) ||
               std::any_of(retired_references_.begin(), retired_references_.end(), [handle](const RetiredReference &entry) {
                   return entry.sampler == handle;
               });
    }

    void VulkanBindlessTextureTable::CollectCompleted(BindlessSubmissionSerial completed_submission)
    {
        allocator_.CollectCompleted(completed_submission);
        const auto kept = std::remove_if(retired_references_.begin(), retired_references_.end(),
                                         [completed_submission](const RetiredReference &entry) {
                                             return entry.retire_after <= completed_submission;
                                         });
        retired_references_.erase(kept, retired_references_.end());
    }

    void VulkanBindlessTextureTable::ApplyLiveEntries(
        VkDevice device, uint32_t frame_index, TextureManager &textures, SamplerManager &samplers)
    {
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorImageInfo> image_infos;
        writes.reserve(entries_.size());
        image_infos.reserve(entries_.size());
        for (uint32_t index = 0; index < entries_.size(); ++index)
        {
            const Entry &entry = entries_[index];
            if (!entry.live || applied_revisions_[frame_index][index] == entry.revision)
            {
                continue;
            }
            Texture *texture = textures.GetTexture(entry.texture);
            Sampler *sampler = samplers.GetSampler(entry.sampler);
            if (!texture || !sampler)
            {
                continue;
            }
            const VulkanTextureResource texture_resource =
                ConvertToVulkanTextureResource(texture->GetTextueHandle());
            const VulkanSamplerResource sampler_resource =
                ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
            image_infos.push_back({sampler_resource.sampler, texture_resource.view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptor_sets_[frame_index];
            write.dstBinding = BindlessTextureTableLayout::descriptor_binding;
            write.dstArrayElement = index;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_infos.back();
            writes.push_back(write);
            applied_revisions_[frame_index][index] = entry.revision;
        }
        if (!writes.empty())
        {
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
}
