#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_H

#include "common/shader.h"
#include <glad/glad.h>
#include <vector>
namespace kpengine::graphics
{
    class OpenglShader : public Shader
    {
    public:
        OpenglShader(ShaderType type, const std::string &path);
        ~OpenglShader();
        GLuint GetShaderHandle() const { return shader_handle_; }

        const void *GetCode() const override { return shader_code_.data(); }
        size_t GetCodeSize() const override { return shader_code_.size(); }

    private:
        GLuint CompileShader(const std::string &path, GLenum shader_type);

    private:
        std::vector<char> shader_code_;
        GLuint shader_handle_ = 0;
    };
}

#endif