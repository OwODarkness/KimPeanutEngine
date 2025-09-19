#include "sampler_manager.h"
#include "opengl/opengl_sampler.h"
#include "vulkan/vulkan_sampler.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    SamplerHandle SamplerManager::CreateSampler(GraphicsContext context, const SamplerSettings &settings)
    {
        uint32_t id{};
        if (!free_slots_.empty())
        {
            id = free_slots_.back();
            free_slots_.pop_back();
        }
        else
        {
            id = static_cast<uint32_t>(samplers_.size());
            samplers_.emplace_back();
        }

        if (context.type == GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            samplers_[id].sampler = std::make_unique<VulkanSampler>();
        }
        else if (context.type == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            samplers_[id].sampler = std::make_unique<OpenglSampler>();
        }

        samplers_[id].sampler->Initialize(context, settings);

        return {id, samplers_[id].generation};
    }

    SamplerSlot &SamplerManager::GetSamplerSlot(SamplerHandle handle)
    {
        if (handle.id >= samplers_.size())
        {
            KP_LOG("SamplerManagerLog", LOG_LEVEL_ERROR, "Failed to get sampler slot, out of range");
            throw std::runtime_error("Failed to get sampler slot, out of range");
        }

        return samplers_[handle.id];
    }

    Sampler *SamplerManager::GetSampler(SamplerHandle handle)
    {
        return GetSamplerSlot(handle).sampler.get();
    }
    bool SamplerManager::DestroySampler(GraphicsContext context, SamplerHandle handle)
    {
        SamplerSlot& slot = GetSamplerSlot(handle);
        if(!slot.sampler)
        {
            return false;
        }
        slot.sampler->Destroy(context);

        slot.generation++;
        free_slots_.push_back(handle.id);
        return true;
    }

}