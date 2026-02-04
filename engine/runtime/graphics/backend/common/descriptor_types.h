#ifndef KPENGINE_RUNTIME_GRAPHICS_DESCRIPTOR_TYPES_H
#define KPENGINE_RUNTIME_GRAPHICS_DESCRIPTOR_TYPES_H

#include "enum.h"

namespace kpengine::graphics{
      struct DescriptorBindingDesc
    {
        uint32_t binding;
        uint32_t descriptor_count;
        DescriptorType descriptor_type;
        ShaderStage stage_flag;
    }; 
}

#endif