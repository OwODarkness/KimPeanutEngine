#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_DESCRIPTOR_SET_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_DESCRIPTOR_SET_H

#include <unordered_map>
#include <variant>
#include <cstdint>

#include "common/descriptor_types.h"

namespace kpengine::graphics{

    
    using OpenglDescriptorData = std::variant<uint32_t, std::pair<uint32_t, uint32_t>>;

    struct OpenglDescriptorResource{
        DescriptorType type;
        OpenglDescriptorData data;
    };

    class OpenglDescriptorSet{
    public:
        void SetUniformBuffer(uint32_t binding, uint32_t buffer_id);
        void SetCombinedImageSampler(uint32_t binding, uint32_t image_id, uint32_t sampler_id);
        void Bind();
    private:
        std::unordered_map<uint32_t, OpenglDescriptorResource> resources_;
    };
}

#endif