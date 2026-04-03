#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_MODULE_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_MODULE_H

#include <memory>
#include "data/shader.h"
#include "base/base.h"

namespace kpengine::graphics{
using ShaderData = kpengine::data::ShaderData;

class ShaderModule{
public:
    virtual ~ShaderModule() = default;
    virtual void Initialize(const GraphicsContext& context,  std::shared_ptr<ShaderData> shader) = 0;
    virtual void Destroy() = 0;
    virtual const void* GetHandle() const = 0;
    virtual ShaderStage GetStage() const = 0;
    virtual const std::string& GetEntryPoint() const = 0;
};
}

#endif