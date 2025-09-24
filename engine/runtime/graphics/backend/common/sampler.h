#ifndef KPENGINE_RUNTIME_GRAPHICS_SAMPLER_H
#define KPENGINE_RUNTIME_GRAPHICS_SAMPLER_H

#include "enum.h"
#include "graphics_context.h"

namespace kpengine::graphics
{
    struct SamplerSettings
    {
        SamplerFilterType mag_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        SamplerFilterType min_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        SamplerAddressMode address_mode_u = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        SamplerAddressMode address_mode_v = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        SamplerAddressMode address_mode_w = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        SamplerMipmapMode mipmap_mode = SamplerMipmapMode::SAMPLER_MIPMAP_MODE_LINEAR;
        bool enable_anisotropy = true;
        float max_anisotropy = 1.f;
        float mip_lod_bias = 0.f;
        float min_lod = 0.f;
        float max_lod = 0.f;
    };

    struct SamplerResource{
        void* sampler;
    };

    class Sampler{
    public:
      virtual SamplerResource GetSampleHandle() const = 0;  
    protected:
        virtual void Initialize(GraphicsContext context, const SamplerSettings& settings) = 0;
        virtual bool Destroy(GraphicsContext context) = 0;
    private:
        friend class SamplerManager;
    };
}

#endif