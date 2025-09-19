#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_SAMPLER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_SAMPLER_H

#include <vulkan/vulkan.h>
#include "common/sampler.h"

namespace kpengine::graphics{

    struct VulkanSamplerResource{
        VkSampler sampler;
    };

    inline VulkanSamplerResource ConvertToVulkanSamplerResource(const SamplerResource& resource)
    {
        VulkanSamplerResource res{};
        res.sampler = reinterpret_cast<VkSampler>(resource.sampler);
        return res;
    }   

    inline SamplerResource ConvertToSamplerResource(const VulkanSamplerResource& resource)
    {
        SamplerResource res{};
        res.sampler = reinterpret_cast<void*>(resource.sampler);
        return res;
    }

    class VulkanSampler: public Sampler{
    public:
      virtual SamplerResource GetSampleHandle() const override{return ConvertToSamplerResource(resource_);}
    protected:
        virtual void Initialize(GraphicsContext context, const SamplerSettings& settings) override;
        virtual bool Destroy(GraphicsContext context) override;
    private:
        VulkanSamplerResource resource_;
    };
}


#endif