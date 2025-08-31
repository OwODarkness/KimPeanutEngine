#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_FACTORY_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_FACTORY_H

#include <memory>
#include "base/base.h"
#include "opengl/opengl_shader.h"
#include "vulkan/vulkan_shader.h"


namespace kpengine::graphics{
    template<GraphicsAPIType type>
    struct ShaderTraits;

    struct ShaderTraits<GraphicsAPIType::GRAPHICS_API_OPENGL>
    {
        using ShaderClass = OpenglShader;
    };

    struct ShaderTraits<GraphicsAPIType::GRAPHICS_API_VULKAN>
    {
        using ShaderClass = VulkanShader;
    };

    // template<GraphicsAPIType type>
    // std::shared_ptr<Shader> CreateShader()
    // {

    // }
}

#endif