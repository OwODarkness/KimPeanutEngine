#include "opengl_shader_module.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    void OpenglShaderModule::Initialize(const GraphicsContext &context, std::shared_ptr<ShaderData> shader)
    {
        if(shader == nullptr)
        {
            throw std::runtime_error("Shader is nullptr");
        }
        if (context.type != GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            throw std::runtime_error("Graphics api mismatch, desired OPENGL");
        }
        uint32_t glshader_type = 0;
        switch (shader->stage)
        {
        case ShaderStage::SHADER_STAGE_VERTEX:
            glshader_type = GL_VERTEX_SHADER;
            break;
        case ShaderStage::SHADER_STAGE_FRAGMENT:
            glshader_type = GL_FRAGMENT_SHADER;
            break;
        case ShaderStage::SHADER_STAGE_GEOMETRY:
            glshader_type = GL_GEOMETRY_SHADER;
            break;
        case ShaderStage::SHADER_STAGE_COMPUTE:
            glshader_type = GL_COMPUTE_SHADER;
        default:
            break;
        }
        GLuint shader_handle = glCreateShader(glshader_type);

        const char *source = shader->glsl.c_str();
        GLint shader_size = static_cast<GLint>(shader->glsl.size());
        glShaderSource(shader_handle, 1, &source, &shader_size);
        glCompileShader(shader_handle);

        GLint success = 0;
        char log[512];
        glGetShaderiv(shader_handle, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            glGetShaderInfoLog(shader_handle, 512, nullptr, log);
            throw std::runtime_error(log);
        }
    }
    void OpenglShaderModule::Destroy()
    {
        if (handle_)
            glDeleteShader(handle_);
    }
    const void *OpenglShaderModule::GetHandle() const
    {
        return &handle_;
    }

    ShaderStage OpenglShaderModule::GetStage() const
    {
        return stage_;
    }
    const std::string &OpenglShaderModule::GetEntryPoint() const
    {
        return entry_point_;
    }
}