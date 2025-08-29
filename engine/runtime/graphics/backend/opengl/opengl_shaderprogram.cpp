#include "opengl_shaderprogram.h"
#include <fstream>
#include "tool/shaderloader.h"
#include "log/logger.h"

namespace kpengine::graphics
{
#define SHADER_LOG_BUF_MAX_SIZE 512

    OpenglShaderProgram::OpenglShaderProgram(const std::string &name, const std::string &vert_path, const std::string &frag_path, const std::string &geom_path) : ShaderProgram(name)
    {

        GLint bsucceed = 0;
        char info_log[SHADER_LOG_BUF_MAX_SIZE];
        std::vector<GLuint> shader_buf{};
        program_handle_ = glCreateProgram();
        try
        {
            // vertex shader
            GLuint vertex_shader_handle = CompileShader(vert_path, GL_VERTEX_SHADER);
            shader_buf.push_back(vertex_shader_handle);
            glAttachShader(program_handle_, vertex_shader_handle);
            // frag shader
            GLuint frag_shader_handle = CompileShader(frag_path, GL_FRAGMENT_SHADER);
            shader_buf.push_back(frag_shader_handle);
            glAttachShader(program_handle_, frag_shader_handle);
            // geom sahder
            if (!geom_path.empty())
            {
                GLuint geom_shader_handle = CompileShader(geom_path, GL_GEOMETRY_SHADER);
                shader_buf.push_back(geom_shader_handle);
                glAttachShader(program_handle_, geom_shader_handle);
            }
            glLinkProgram(program_handle_);
            glGetProgramiv(program_handle_, GL_LINK_STATUS, &bsucceed);
            if (!bsucceed)
            {
                glGetProgramInfoLog(program_handle_, SHADER_LOG_BUF_MAX_SIZE, nullptr, info_log);
                KP_LOG("OpenglShaderLog", LOG_LEVEL_ERROR, info_log);
                throw std::runtime_error(info_log);
            }
            KP_LOG("OpenglShaderlog", LOG_LEVEL_INFO, name_ + " shader linked successfully");
        }
        catch (std::runtime_error err)
        {
            for (GLuint shader_handle : shader_buf)
            {
                glDeleteShader(shader_handle);
            }

            if (program_handle_ != 0)
            {
                glDeleteProgram(program_handle_);
            }

            throw err;
        }
        for (GLuint shader_handle : shader_buf)
        {
            glDeleteShader(shader_handle);
        }
    }
    void OpenglShaderProgram::Bind()
    {
    }
    void OpenglShaderProgram::Unbind()
    {
    }
    void OpenglShaderProgram::Cleanup()
    {
        if(program_handle_!= 0)
        {
            glDeleteProgram(program_handle_);
        }
    }

    GLuint OpenglShaderProgram::CompileShader(const std::string &path, GLenum shader_type) const
    {
        std::vector<char> shader_code = ShaderLoader::ReadTextFile(path);
        GLuint shader_handle = glCreateShader(shader_type);

        const char *source = shader_code.data();
        GLint shader_size = shader_code.size();
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

    OpenglShaderProgram::~OpenglShaderProgram()
    {
        Cleanup();
    }

}