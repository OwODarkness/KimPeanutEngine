#include "opengl_descriptorset.h"
#include <glad/glad.h>
namespace kpengine::graphics{
        void OpenglDescriptorSet::SetUniformBuffer(uint32_t binding, uint32_t buffer_id,
                                                    size_t offset, size_t range,
                                                    DescriptorType type)
        {
            resources_[binding] = {
                type,
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

        void OpenglDescriptorSet::Bind(const DynamicUniformOffsets &dynamic_offsets)
        {
            size_t dynamic_index = 0;
            for(const auto& resource_kv : resources_)
            {
                uint32_t binding = resource_kv.first;
                OpenglDescriptorResource resource = resource_kv.second; 
                if(resource.type == DescriptorType::DESCRIPTOR_TYPE_UNIFORM ||
                   resource.type == DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC)
                {
                    const OpenglUniformBufferBinding ubo =
                        std::get<OpenglUniformBufferBinding>(resource.data);
                    size_t offset = ubo.offset;
                    if (resource.type == DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC)
                    {
                        if (dynamic_index >= dynamic_offsets.size())
                        {
                            continue;
                        }
                        offset += dynamic_offsets[dynamic_index++];
                    }
                    if (ubo.range == 0)
                    {
                        glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo.buffer);
                    }
                    else
                    {
                        glBindBufferRange(GL_UNIFORM_BUFFER, binding, ubo.buffer,
                                          static_cast<GLintptr>(offset),
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
