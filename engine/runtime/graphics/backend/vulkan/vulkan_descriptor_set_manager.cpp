#include "vulkan_descriptor_set_manager.h"

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
    DescriptorSetHandle VulkanDescriptorSetManager::CreateResourceBindingSet(
        VkDevice logical_device, const VulkanPipelineResource &pipeline,
        const ResourceBindingSetDesc &desc, VulkanBufferManager &buffers,
        TextureManager &textures, SamplerManager &samplers)
    {
        if (desc.set >= pipeline.descriptor_set_layouts.size())
        {
            throw std::runtime_error("descriptor set index is not declared by the pipeline");
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

        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (uniform_count != 0)
        {
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_count});
        }
        if (sampled_texture_count != 0)
        {
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampled_texture_count});
        }

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(logical_device, &pool_info, nullptr, &pool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor pool");
        }

        VkDescriptorSetLayout layout = pipeline.descriptor_set_layouts[desc.set].layout;
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &layout;

        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(logical_device, &allocate_info, &descriptor_set) != VK_SUCCESS)
        {
            vkDestroyDescriptorPool(logical_device, pool, nullptr);
            throw std::runtime_error("failed to allocate descriptor set");
        }

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

        vkUpdateDescriptorSets(logical_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        const DescriptorSetHandle handle = handle_system_.Create();
        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        resources_[handle.id] = {pool, descriptor_set};
        return handle;
    }

    bool VulkanDescriptorSetManager::DestroyResourceBindingSet(VkDevice logical_device,
                                                                DescriptorSetHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= resources_.size() || resources_[index].pool == VK_NULL_HANDLE)
        {
            return false;
        }
        vkDestroyDescriptorPool(logical_device, resources_[index].pool, nullptr);
        resources_[index] = {};
        return handle_system_.Destroy(handle);
    }

    void VulkanDescriptorSetManager::DestroyAll(VkDevice logical_device)
    {
        for (VulkanDescriptorSetResource &resource : resources_)
        {
            if (resource.pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(logical_device, resource.pool, nullptr);
                resource = {};
            }
        }
    }

    VkDescriptorSet VulkanDescriptorSetManager::GetDescriptorSet(DescriptorSetHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= resources_.size())
        {
            KP_LOG("VulkanDescriptorSetManagerLog", LOG_LEVEL_ERROR,
                   "failed to find descriptor set resource");
            return VK_NULL_HANDLE;
        }
        return resources_[index].descriptor_set;
    }
}
