#include "sampler_manager.h"
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
#include "opengl/opengl_sampler.h"
#endif
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
#include "vulkan/vulkan_sampler.h"
#endif
#include "log/logger.h"
namespace kpengine::graphics
{
    SamplerHandle SamplerManager::CreateSampler(GraphicsContext context, const SamplerSettings &settings)
    {
        SamplerHandle handle = handle_system_.Create();
        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }

        SamplerSlot& resource = resources_[handle.id];

        if (context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
            resource.sampler = std::make_unique<VulkanSampler>();
#endif
        }
        else if (context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
            resource.sampler = std::make_unique<OpenglSampler>();
#endif
        }

        if (!resource.sampler)
        {
            handle_system_.Destroy(handle);
            return {};
        }

        resource.sampler->Initialize(context, settings);

        return handle;
    }

    SamplerSlot* SamplerManager::GetSamplerSlot(SamplerHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);

        if (index >= resources_.size())
        {
            KP_LOG("SamplerManagerLog", LOG_LEVEL_ERROR, "Failed to get sampler slot, out of range");
            throw std::runtime_error("Failed to get sampler slot, out of range");
        }

        return &resources_[index];
    }

    Sampler *SamplerManager::GetSampler(SamplerHandle handle)
    {
        SamplerSlot* resource = GetSamplerSlot(handle);
        if(resource)
        {
            return resource->sampler.get();
        }
        return nullptr;
    }
    bool SamplerManager::DestroySampler(GraphicsContext context, SamplerHandle handle)
    {
        Sampler*sampler = GetSampler(handle);
        if(!sampler)
        {
            return false;
        }
        sampler->Destroy(context);
        return handle_system_.Destroy(handle);
    }

}
