#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_SHADER_H

#include "common/shader.h"
#include <glad/glad.h>

namespace kpengine::graphics{
    class OpenglShader: public Shader{
    public:
        OpenglShader(const std::string& name, ShaderType type, const std::string& path);
        ~OpenglShader();
        GLuint GetShaderHandle() const{return shader_handle_;} 
    private:
        GLuint CompileShader(const std::string& path, GLenum shader_type) const ;
    private:
        GLuint shader_handle_ = 0;
    };
}

#endif