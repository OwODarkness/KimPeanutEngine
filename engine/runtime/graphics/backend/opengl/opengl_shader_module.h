#ifndef KPENGINE_GRAPHICS_OPENGL_SHADER_MODULE_H
#define KPENGINE_GRAPHICS_OPENGL_SHADER_MODULE_H

#include "common/shader_module.h"
#include <glad/glad.h>
namespace kpengine::graphics
{
    class OpenglShaderModule : public ShaderModule
    {
    public:
        void Initialize(const GraphicsContext &context, std::shared_ptr<ShaderData> shader) override;
        void Destroy() override;
        const void *GetHandle() const override;
        ShaderStage GetStage() const override;
        const std::string &GetEntryPoint() const override;

    private:
        GLuint handle_ = 0;
        ShaderStage stage_;
        std::string entry_point_;
    };
}

#endif