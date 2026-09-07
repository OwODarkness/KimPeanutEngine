#include "vulkan_descriptor_set_manager.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "common/sampler_manager.h"
#include "common/texture_manager.h"
#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_sampler.h"
#include "vulkan_texture.h"

namespace kpengine::graphics
{
    namespace
    {
        constexpr uint32_t kInitialDescriptorSets = 1024;
        constexpr uint32_t kInitialUniformDescriptors = 4096;
        constexpr uint32_t kInitialSampledTextureDescriptors = 4096;

        uint32_t GrowCapacity(uint32_t current, uint32_t required)
        {
            const uint64_t doubled = static_cast<uint64_t>(current) * 2;
            const uint64_t target = std::max<uint64_t>(doubled, required);
            if (target > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error("descriptor pool arena capacity overflow");
            }
            return static_cast<uint32_t>(target);
        }

        bool HasCapacity(const VulkanDescriptorPoolArena &arena, uint32_t uniform_count,
                         uint32_t sampled_texture_count)
        {
            return arena.used_sets < arena.max_sets &&
                   uniform_count <= arena.uniform_capacity - arena.used_uniform_descriptors &&
                   sampled_texture_count <=
                       arena.sampled_texture_capacity - arena.used_sampled_texture_descriptors;
        }
    }

    void VulkanDescriptorSetManager::Initialize(uint32_t frame_slot_count)
    {
        if (frame_slot_count == 0)
        {
            throw std::runtime_error("descriptor set manager requires a frame slot");
        }
        frame_arenas_.clear();
        frame_arenas_.resize(frame_slot_count);
        persistent_arenas_.clear();
        current_frame_slot_ = 0;
        frame_active_ = false;
    }

    void VulkanDescriptorSetManager::BeginFrame(VkDevice logical_device, uint32_t frame_slot)
    {
        if (frame_slot >= frame_arenas_.size())
        {
            throw std::runtime_error("descriptor set manager received an invalid frame slot");
        }

        // VulkanBackend calls this immediately after waiting for the slot's
        // fence. No command buffer can still reference descriptors from these
        // pools at this point.
        for (VulkanDescriptorPoolArena &arena : frame_arenas_[frame_slot])
        {
            if (vkResetDescriptorPool(logical_device, arena.pool, 0) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to reset descriptor pool arena");
            }
            arena.used_sets = 0;
            arena.used_uniform_descriptors = 0;
            arena.used_sampled_texture_descriptors = 0;
        }

        // FrameContext releases its old transient handles after Backend's
        // BeginFrame. Invalidate them here first, so a recycled handle id can
        // never resolve to a descriptor set from the new frame.
        for (VulkanDescriptorSetResource &resource : resources_)
        {
            if (resource.descriptor_set != VK_NULL_HANDLE && resource.frame_slot == frame_slot)
            {
                handle_system_.Destroy(resource.handle);
                resource = {};
            }
        }
        current_frame_slot_ = frame_slot;
        frame_active_ = true;
        profile_counters_ = {};
    }

    VulkanDescriptorPoolArena &VulkanDescriptorSetManager::CreateArena(
        VkDevice logical_device, std::vector<VulkanDescriptorPoolArena> &arenas,
        uint32_t required_sets,
        uint32_t required_uniform_descriptors, uint32_t required_sampled_texture_descriptors)
    {
        uint32_t max_sets = std::max(kInitialDescriptorSets, required_sets);
        uint32_t uniform_capacity =
            std::max(kInitialUniformDescriptors, required_uniform_descriptors);
        uint32_t sampled_texture_capacity =
            std::max(kInitialSampledTextureDescriptors, required_sampled_texture_descriptors);
        if (!arenas.empty())
        {
            const VulkanDescriptorPoolArena &previous = arenas.back();
            max_sets = GrowCapacity(previous.max_sets, required_sets);
            uniform_capacity = GrowCapacity(previous.uniform_capacity, required_uniform_descriptors);
            sampled_texture_capacity =
                GrowCapacity(previous.sampled_texture_capacity, required_sampled_texture_descriptors);
        }

        const VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_capacity},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampled_texture_capacity}};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = max_sets;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;

        VulkanDescriptorPoolArena arena{};
        arena.max_sets = max_sets;
        arena.uniform_capacity = uniform_capacity;
        arena.sampled_texture_capacity = sampled_texture_capacity;
        if (vkCreateDescriptorPool(logical_device, &pool_info, nullptr, &arena.pool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool arena");
        }
        arenas.push_back(arena);
        return arenas.back();
    }

    DescriptorSetHandle VulkanDescriptorSetManager::CreateResourceBindingSet(
        VkDevice logical_device, const VulkanPipelineResource &pipeline,
        const ResourceBindingSetDesc &desc, VulkanBufferManager &buffers,
        TextureManager &textures, SamplerManager &samplers, bool *pool_created)
    {
        if (pool_created)
        {
            *pool_created = false;
        }
        if (desc.set >= pipeline.descriptor_set_layouts.size())
        {
            throw std::runtime_error("descriptor set index is not declared by the pipeline");
        }
        if (frame_arenas_.empty())
        {
            throw std::runtime_error("descriptor set manager is not initialized");
        }

        uint32_t uniform_count = 0;
        uint32_t sampled_texture_count = 0;
        for (const ResourceBinding &binding : desc.bindings)
        {
            std::visit([&](const auto &value) {
                using Binding = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Binding, UniformBufferBinding>)
                {
                    ++uniform_count;
                }
                else
                {
                    ++sampled_texture_count;
                }
            }, binding);
        }

        const uint32_t resource_frame_slot = frame_active_ ? current_frame_slot_ : UINT32_MAX;
        auto &arenas = frame_active_ ? frame_arenas_[current_frame_slot_] : persistent_arenas_;
        const auto search_started = std::chrono::steady_clock::now();
        std::size_t arena_index = std::numeric_limits<std::size_t>::max();
        for (std::size_t index = 0; index < arenas.size(); ++index)
        {
            if (HasCapacity(arenas[index], uniform_count, sampled_texture_count))
            {
                arena_index = index;
                break;
            }
        }
        if (arena_index == std::numeric_limits<std::size_t>::max())
        {
            CreateArena(logical_device, arenas, 1, uniform_count, sampled_texture_count);
            arena_index = arenas.size() - 1;
            if (pool_created)
            {
                *pool_created = true;
            }
        }
        ++profile_counters_.searches;
        profile_counters_.search_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - search_started)
                .count();

        VkDescriptorSetLayout layout = pipeline.descriptor_set_layouts[desc.set].layout;
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = arenas[arena_index].pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &layout;

        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        const auto allocation_started = std::chrono::steady_clock::now();
        VkResult allocate_result = vkAllocateDescriptorSets(logical_device, &allocate_info, &descriptor_set);
        if (allocate_result == VK_ERROR_OUT_OF_POOL_MEMORY || allocate_result == VK_ERROR_FRAGMENTED_POOL)
        {
            CreateArena(logical_device, arenas, 1, uniform_count, sampled_texture_count);
            arena_index = arenas.size() - 1;
            allocate_info.descriptorPool = arenas[arena_index].pool;
            allocate_result = vkAllocateDescriptorSets(logical_device, &allocate_info, &descriptor_set);
            if (pool_created)
            {
                *pool_created = true;
            }
        }
        if (allocate_result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate descriptor set");
        }
        ++profile_counters_.allocations;
        profile_counters_.allocation_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - allocation_started)
                .count();

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        writes.reserve(desc.bindings.size());
        buffer_infos.reserve(uniform_count);
        image_infos.reserve(sampled_texture_count);

        for (const ResourceBinding &binding : desc.bindings)
        {
            std::visit([&](const auto &value) {
                using Binding = std::decay_t<decltype(value)>;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptor_set;
                write.dstBinding = value.binding;
                write.dstArrayElement = 0;
                write.descriptorCount = 1;

                if constexpr (std::is_same_v<Binding, UniformBufferBinding>)
                {
                    VulkanBufferResource *buffer = buffers.GetBufferResource(value.buffer);
                    if (!buffer)
                    {
                        throw std::runtime_error("invalid uniform-buffer handle");
                    }
                    buffer_infos.push_back({buffer->buffer, value.offset, value.range});
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &buffer_infos.back();
                }
                else
                {
                    Texture *texture = textures.GetTexture(value.texture);
                    Sampler *sampler = samplers.GetSampler(value.sampler);
                    const bool sampleable =
                        texture && (static_cast<uint32_t>(texture->settings_.usage) &
                                    static_cast<uint32_t>(TextureUsage::TEXTURE_USAGE_SAMPLE)) != 0;
                    if (!sampleable || !sampler)
                    {
                        throw std::runtime_error("invalid sampled-texture binding");
                    }
                    const VulkanTextureResource texture_resource =
                        ConvertToVulkanTextureResource(texture->GetTextueHandle());
                    const VulkanSamplerResource sampler_resource =
                        ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
                    image_infos.push_back({sampler_resource.sampler, texture_resource.view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &image_infos.back();
                }
                writes.push_back(write);
            }, binding);
        }

        const auto update_started = std::chrono::steady_clock::now();
        vkUpdateDescriptorSets(logical_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        ++profile_counters_.updates;
        profile_counters_.update_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - update_started)
                .count();

        const DescriptorSetHandle handle = handle_system_.Create();
        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        resources_[handle.id] = {descriptor_set, resource_frame_slot, arena_index, handle};
        ++arenas[arena_index].used_sets;
        arenas[arena_index].used_uniform_descriptors += uniform_count;
        arenas[arena_index].used_sampled_texture_descriptors += sampled_texture_count;
        return handle;
    }

    bool VulkanDescriptorSetManager::DestroyResourceBindingSet(VkDevice logical_device,
                                                                DescriptorSetHandle handle)
    {
        (void)logical_device;
        const uint32_t index = handle_system_.Get(handle);
        if (index >= resources_.size() || resources_[index].descriptor_set == VK_NULL_HANDLE)
        {
            return false;
        }
        resources_[index] = {};
        return handle_system_.Destroy(handle);
    }

    void VulkanDescriptorSetManager::DestroyAll(VkDevice logical_device)
    {
        for (VulkanDescriptorSetResource &resource : resources_)
        {
            if (resource.descriptor_set != VK_NULL_HANDLE)
            {
                handle_system_.Destroy(resource.handle);
                resource = {};
            }
        }
        for (auto &arenas : frame_arenas_)
        {
            for (VulkanDescriptorPoolArena &arena : arenas)
            {
                if (arena.pool != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(logical_device, arena.pool, nullptr);
                    arena = {};
                }
            }
        }
        for (VulkanDescriptorPoolArena &arena : persistent_arenas_)
        {
            if (arena.pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(logical_device, arena.pool, nullptr);
                arena = {};
            }
        }
        persistent_arenas_.clear();
        frame_arenas_.clear();
    }

    VkDescriptorSet VulkanDescriptorSetManager::GetDescriptorSet(DescriptorSetHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= resources_.size() || resources_[index].descriptor_set == VK_NULL_HANDLE)
        {
            KP_LOG("VulkanDescriptorSetManagerLog", LOG_LEVEL_ERROR,
                   "failed to find descriptor set resource");
            return VK_NULL_HANDLE;
        }
        return resources_[index].descriptor_set;
    }
}
