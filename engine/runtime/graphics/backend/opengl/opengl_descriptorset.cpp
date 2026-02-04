#include "opengl_descriptorset.h"
#include <glad/glad.h>
namespace kpengine::graphics{
        void OpenglDescriptorSet::SetUniformBuffer(uint32_t binding, uint32_t buffer_id)
        {
            resources_[binding] = {
                DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                buffer_id
            };
        }
        
        void OpenglDescriptorSet::SetCombinedImageSampler(uint32_t binding, uint32_t image_id, uint32_t sampler_id)
        {
            resources_[binding] = {
                DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                std::pair<uint32_t, uint32_t>(image_id, sampler_id)
            };
        }

        void OpenglDescriptorSet::Bind()
        {
            for(const auto& resource_kv : resources_)
            {
                uint32_t binding = resource_kv.first;
                OpenglDescriptorResource resource = resource_kv.second; 
                if(resource.type == DescriptorType::DESCRIPTOR_TYPE_UNIFORM)
                {
                    uint32_t ubo = std::get<uint32_t>(resource.data);
                    glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo);
                }
                else if(resource.type == DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER)
                {
                    std::pair<uint32_t, uint32_t> image_sampler = std::get<std::pair<uint32_t, uint32_t>>(resource.data);
                    glBindTextureUnit(binding, image_sampler.first);
                    glBindSampler(binding, image_sampler.second);
                }
            }
        }
}