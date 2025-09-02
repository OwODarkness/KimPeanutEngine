#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_SHADER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_SHADER_H

#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>
#include "common/shader.h"

namespace kpengine::graphics{
class VulkanShader: public Shader{
public:
    VulkanShader(ShaderType type, const std::string& path);
    ~VulkanShader() = default;

        const void* GetCode() const override{return shader_code_.data();}
        size_t GetCodeSize() const override{return shader_code_.size() * sizeof(uint32_t);}

private:
    void LoadShaderCode();
private:
    std::vector<uint32_t> shader_code_;
};
}

#endif