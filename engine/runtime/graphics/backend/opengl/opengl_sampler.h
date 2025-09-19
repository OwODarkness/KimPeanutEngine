#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_SAMPLER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_SAMPLER_H

#include <glad/glad.h>
#include "common/sampler.h"
namespace kpengine::graphics{

    struct OpenglSamplerResource{
        GLuint sampler = 0;        
    };

    inline OpenglSamplerResource ConvertToOpenglSamplerResource(const SamplerResource& resource)
    {
        OpenglSamplerResource res{};
        res.sampler = static_cast<GLuint>(reinterpret_cast<uintptr_t>(resource.sampler));
        return res;
    }



    inline SamplerResource ConvertToSamplerResource(const OpenglSamplerResource& resource)
    {
        SamplerResource res{};
        res.sampler = reinterpret_cast<void*>(static_cast<uintptr_t>(resource.sampler));
        return res;
    }

    class OpenglSampler: public Sampler{
    public:
      virtual SamplerResource GetSampleHandle() const override{return ConvertToSamplerResource(resource_);}
    protected:
        virtual void Initialize(GraphicsContext context, const SamplerSettings& settings) override;
        virtual bool Destroy(GraphicsContext context) override;
    private:
        OpenglSamplerResource resource_;
    
    };
}

#endif