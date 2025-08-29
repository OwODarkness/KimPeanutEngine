#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_PROGRAM_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_PROGRAM_H

#include "common/shaderprogram.h"
#include <glad/glad.h>

namespace kpengine::graphics{
    class OpenglShaderProgram: public ShaderProgram{
    public:
        OpenglShaderProgram(const std::string& name, const std::string& vert_path, const std::string& frag_path, const std::string& geom_path = "");
        ~OpenglShaderProgram();
        void Bind() override;
        void Unbind() override;
        void Cleanup() override;
    private:
        GLuint CompileShader(const std::string& path, GLenum shader_type) const ;
    private:
        GLuint program_handle_ = 0;
    };
}

#endif