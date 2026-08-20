#include "opengl_descriptorset.h"
#include <glad/glad.h>
namespace kpengine::graphics{
        void OpenglDescriptorSet::SetUniformBuffer(uint32_t binding, uint32_t buffer_id,
                                                    size_t offset, size_t range)
        {
            resources_[binding] = {
                DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                OpenglDescriptorData{OpenglUniformBufferBinding{buffer_id, offset, range}}
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
                    const OpenglUniformBufferBinding ubo =
                        std::get<OpenglUniformBufferBinding>(resource.data);
                    if (ubo.range == 0)
                    {
                        glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo.buffer);
                    }
                    else
                    {
                        glBindBufferRange(GL_UNIFORM_BUFFER, binding, ubo.buffer,
                                          static_cast<GLintptr>(ubo.offset),
                                          static_cast<GLsizeiptr>(ubo.range));
                    }
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
