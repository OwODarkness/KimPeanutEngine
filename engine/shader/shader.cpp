#include "shader.h"

#include <fstream>
#include<iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "runtime/core/log/logger.h"

namespace kpengine
{

    ShaderHelper::ShaderHelper(std::string vertex_shader_path, std::string fragment_shader_path) : vertex_shader_path_(vertex_shader_path),
                                                                                                   fragment_shader_path_(fragment_shader_path)
    {
    }

    void ShaderHelper::Initialize()
    {
        int bsucceed = 0;
        char info_log[512];
        // compile vertex shader
        std::string vertex_shader_code = "";
        std::filesystem::path abs_vertex_shader_path = shader_directory_path / vertex_shader_path_;
        if (!ExtractShaderCodeFromFile(abs_vertex_shader_path, vertex_shader_code))
        {
            return;
        }
        unsigned int vertex_shader;
        vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        const char *vertex_shader_code_c = vertex_shader_code.c_str();
        glShaderSource(vertex_shader, 1, &vertex_shader_code_c, nullptr);
        glCompileShader(vertex_shader);
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &bsucceed);
        if (!bsucceed)
        {
            glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
            KP_LOG("VertexShaderLog", LOG_LEVEL_ERROR, info_log);
        }

        // compile fragment_shader_code
        std::string fragment_shader_code = "";
        std::filesystem::path abs_fragment_shader_path = shader_directory_path / fragment_shader_path_;
        if (!ExtractShaderCodeFromFile(abs_fragment_shader_path, fragment_shader_code))
        {
            return;
        }
        const char *fragment_shader_code_c = fragment_shader_code.c_str();
        unsigned int fragment_shader;
        fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &fragment_shader_code_c, nullptr);
        glCompileShader(fragment_shader);
        if (!bsucceed)
        {
            glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
            KP_LOG("FragmentShaderLog", LOG_LEVEL_ERROR, info_log);
        }

        shader_program_handle_ = glCreateProgram();
        glAttachShader(shader_program_handle_, vertex_shader);
        glAttachShader(shader_program_handle_, fragment_shader);
        glLinkProgram(shader_program_handle_);
        glGetProgramiv(shader_program_handle_, GL_LINK_STATUS, &bsucceed);
        if (!bsucceed)
        {
            glGetProgramInfoLog(shader_program_handle_, 512, nullptr, info_log);
            KP_LOG("ShaderLinkLog", LOG_LEVEL_ERROR, info_log);
        }
        else
        {
            KP_LOG("ShaderLinkLog", LOG_LEVEL_DISPLAY, "shader link successful");
        }

        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
    }

    bool ShaderHelper::ExtractShaderCodeFromFile(std::filesystem::path file_path, std::string &out_code)
    {
        std::ifstream ifs;
        ifs.open(file_path, std::ios_base::in);
        if (!ifs.is_open())
        {

            KP_LOG("ShaderLog", LOG_LEVEL_ERROR, "Failed to open shader file %s", file_path.c_str());
            return false;
        }
        else
        {
            std::string buffer;
            while (std::getline(ifs, buffer))
            {
                out_code = out_code + buffer + '\n';
            }
        }
        ifs.close();
        return true;
    }

    void ShaderHelper::UseProgram()
    {
        glUseProgram(shader_program_handle_);
    }

    void ShaderHelper::SetBool(const std::string &name, bool value) const
    {
        glUniform1i(glGetUniformLocation(shader_program_handle_, name.c_str()), value);
    }
    void ShaderHelper::SetInt(const std::string &name, int value) const
    {
        glUniform1i(glGetUniformLocation(shader_program_handle_, name.c_str()), value);
    }
    void ShaderHelper::SetFloat(const std::string &name, float value) const
    {
        glUniform1f(glGetUniformLocation(shader_program_handle_, name.c_str()), value);
    }

    void ShaderHelper::SetMat(const std::string &name, const float *value) const
    {
        glUniformMatrix4fv(glGetUniformLocation(shader_program_handle_, name.c_str()), 1, GL_FALSE, value);
    }

    void ShaderHelper::SetVec3(const std::string &name, const float *value) const
    {
        glUniform3f(glGetUniformLocation(shader_program_handle_, name.c_str()), value[0], value[1], value[2]);
    }

    void ShaderHelper::SetVec3(const std::string &name, float r, float g, float b) const
    {
        glUniform3f(glGetUniformLocation(shader_program_handle_, name.c_str()), r, g, b);
    }

}
