#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_DESCRIPTOR_SET_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_DESCRIPTOR_SET_H

#include <map>
#include <variant>
#include <cstdint>
#include <cstddef>

#include "common/descriptor_types.h"
#include "common/resource_binding.h"

namespace kpengine::graphics{

    
    struct OpenglUniformBufferBinding
    {
        uint32_t buffer = 0;
        size_t offset = 0;
        size_t range = 0;
    };

    using OpenglDescriptorData = std::variant<OpenglUniformBufferBinding,
                                              std::pair<uint32_t, uint32_t>>;

    struct OpenglDescriptorResource{
        DescriptorType type;
        OpenglDescriptorData data;
    };

    class OpenglDescriptorSet{
    public:
        void SetUniformBuffer(uint32_t binding, uint32_t buffer_id,
                              size_t offset, size_t range, DescriptorType type);
        void SetCombinedImageSampler(uint32_t binding, uint32_t image_id, uint32_t sampler_id);
        void Bind(const DynamicUniformOffsets &dynamic_offsets);
    private:
        std::map<uint32_t, OpenglDescriptorResource> resources_;
    };
}

#endif
