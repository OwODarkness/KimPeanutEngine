#include "opengl_sampler.h"
#include "opengl_enum.h"
namespace kpengine::graphics{
        void OpenglSampler::Initialize(GraphicsContext context, const SamplerSettings& settings)
        {
            glCreateSamplers(1, &resource_.sampler);

            //filter
            GLenum min_filter = ConvertToOpenglSampleMinFilter(settings.min_filter, settings.mipmap_mode);
            GLenum mag_filter = ConvertToOpenglSamplerFilter(settings.mag_filter);

            glSamplerParameteri(resource_.sampler, GL_TEXTURE_MIN_FILTER, min_filter);
            glSamplerParameteri(resource_.sampler, GL_TEXTURE_MAG_FILTER, mag_filter);

            //wrap/address_mode
            GLenum address_u = ConvertToOpenglSamplerAddressMode(settings.address_mode_u);
            GLenum address_v = ConvertToOpenglSamplerAddressMode(settings.address_mode_v);
            GLenum address_w = ConvertToOpenglSamplerAddressMode(settings.address_mode_w);

            glSamplerParameteri(resource_.sampler, GL_TEXTURE_WRAP_S, address_u);
            glSamplerParameteri(resource_.sampler, GL_TEXTURE_WRAP_T, address_v);
            glSamplerParameteri(resource_.sampler, GL_TEXTURE_WRAP_R, address_w);

            if(settings.enable_anisotropy)
            {
                glSamplerParameterf(resource_.sampler, GL_TEXTURE_MAX_ANISOTROPY, settings.max_anisotropy);
            }
        }
        bool OpenglSampler::Destroy(GraphicsContext context)
        {
            if(!resource_.sampler)
                return false;
            glDeleteSamplers(1, &resource_.sampler);
            return true;
        }
       
}