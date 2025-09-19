#include "vulkan_sampler.h"
#include "vulkan_enum.h"
#include "vulkan_context.h"
#include "log/logger.h"
namespace kpengine::graphics{
    void VulkanSampler::Initialize(GraphicsContext context, const SamplerSettings& settings)
    {
        VulkanContext* vk_context = static_cast<VulkanContext*>(context.native);
        VkDevice logical_device = vk_context->logical_device;

        VkSamplerCreateInfo sampler_create_info{};
        sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.addressModeU =  ConvertToVulkanAddressMode(settings.address_mode_u);
        sampler_create_info.addressModeV = ConvertToVulkanAddressMode(settings.address_mode_v);
        sampler_create_info.addressModeW = ConvertToVulkanAddressMode(settings.address_mode_w);
        sampler_create_info.magFilter = ConvertToVulkanFilter(settings.mag_filter);
        sampler_create_info.minFilter = ConvertToVulkanFilter(settings.min_filter);
        sampler_create_info.anisotropyEnable = settings.enable_anisotropy;
        sampler_create_info.maxAnisotropy = settings.max_anisotropy;
        sampler_create_info.mipLodBias = settings.mip_lod_bias;
        sampler_create_info.minLod = settings.min_lod;
        sampler_create_info.maxLod = settings.max_lod;
        sampler_create_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_create_info.compareEnable = VK_FALSE;
        sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;

        if(vkCreateSampler(logical_device, &sampler_create_info, nullptr, &resource_.sampler) != VK_SUCCESS)
        {
            KP_LOG("VulkanSamplerLog", LOG_LEVEL_ERROR, "Failed to create sampler");
            throw std::runtime_error("Failed to create sampler");
        }

    }
    bool VulkanSampler::Destroy(GraphicsContext context)
    {
        VulkanContext* vk_context = static_cast<VulkanContext*>(context.native);
        VkDevice logical_device = vk_context->logical_device;
        vkDestroySampler(logical_device, resource_.sampler, nullptr);
        return true;

    }
    
}