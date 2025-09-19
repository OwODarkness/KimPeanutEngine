#ifndef KPENGINE_RUNTIME_GRAPHICS_SAMPLER_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_SAMPLER_MANAGER_H


#include <vector>
#include <memory>
#include "sampler.h"
#include "api.h"
#include "graphics_context.h"
namespace kpengine::graphics{

    struct SamplerSlot{
        std::unique_ptr<Sampler> sampler;
        uint32_t generation = 0;
    };

    class SamplerManager{
    public:
        SamplerHandle CreateSampler(GraphicsContext context, const SamplerSettings& settings);
        Sampler* GetSampler(SamplerHandle handle);
        bool DestroySampler(GraphicsContext context, SamplerHandle handle);
    private:
        SamplerSlot& GetSamplerSlot(SamplerHandle handle);
    private:
        std::vector<SamplerSlot> samplers_;
        std::vector<uint32_t> free_slots_;
    };
}

#endif