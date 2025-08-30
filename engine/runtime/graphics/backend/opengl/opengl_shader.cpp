#include "opengl_shader.h"
#include <fstream>
#include "tool/shaderloader.h"
#include "log/logger.h"

#define SHADER_LOG_BUF_MAX_SIZE 512

namespace kpengine::graphics
{

    OpenglShader::OpenglShader(const std::string &name, ShaderType type, const std::string &path) : Shader(name, type)
    {
        if (type == ShaderType::SHADER_TYPE_VERTEX)
        {
            shader_handle_ = CompileShader(path, GL_VERTEX_SHADER);
        }
        else if (type == ShaderType::SHADER_TYPE_FRAGMENT)
        {
            shader_handle_ = CompileShader(path, GL_FRAGMENT_SHADER);
        }
        else if (type == ShaderType::SHADER_TYPE_GEOMETRY)
        {
            shader_handle_ = CompileShader(path, GL_GEOMETRY_SHADER);
        }
    }

    GLuint OpenglShader::CompileShader(const std::string &path, GLenum shader_type) const
    {
        std::vector<char> shader_code = ShaderLoader::ReadTextFile(path);
        GLuint shader_handle = glCreateShader(shader_type);

        const char *source = shader_code.data();
        GLint shader_size = static_cast<GLint>(shader_code.size());
        glShaderSource(shader_handle, 1, &source, &shader_size);
        glCompileShader(shader_handle);

        GLint success = 0;
        char log[SHADER_LOG_BUF_MAX_SIZE];
        glGetShaderiv(shader_handle, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            glGetShaderInfoLog(shader_handle, SHADER_LOG_BUF_MAX_SIZE, nullptr, log);
            KP_LOG("OpenglShaderLog", LOG_LEVEL_ERROR, log);
            throw std::runtime_error(log);
        }
        return shader_handle;
    }

    OpenglShader::~OpenglShader()
    {
        glDeleteShader(shader_handle_);
    }

}