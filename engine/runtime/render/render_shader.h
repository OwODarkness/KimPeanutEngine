#ifndef KPENGINE_RUNTIME_RENDER_SHADER_H
#define KPENGINE_RUNTIME_RENDER_SHADER_H

#include <string>
#include <filesystem>
namespace kpengine
{

    class RenderShader
    {
    public:
        RenderShader() = delete;
        RenderShader(const RenderShader &) = delete;
        explicit RenderShader(const std::string &vertex_shader_path, const std::string &fragment_shader_path);

        explicit RenderShader(const std::string &vertex_shader_path, const std::string &fragment_shader_path, const std::string &geometry_shader_path);

        void Initialize();


        inline unsigned int GetShaderProgram() const { return shader_program_handle_; }

        void UseProgram() const;

        void SetBool(const std::string &name, bool value) const;
        void SetInt(const std::string &name, int value) const;
        void SetFloat(const std::string &name, float value) const;
        void SetMat(const std::string &name, const float *value) const;
        void SetVec3(const std::string &name, const float *value) const;
        void SetVec3(const std::string &name, float r, float g, float b) const;
        void SetVec2(const std::string &name, float a, float b) const;
        void SetVec2(const std::string &name, const float *value) const;
    private:
        bool ExtractShaderCodeFromFile(const std::string &file_path, std::string &out_code);

    private:
        std::string vertex_shader_path_;
        std::string fragment_shader_path_;
        std::string geometry_shader_path_;
        std::string name_;
        unsigned int shader_program_handle_;
    };
}

#endif