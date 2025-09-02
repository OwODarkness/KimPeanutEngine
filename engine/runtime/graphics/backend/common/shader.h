#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_H

#include <string>
#include <memory>
#include <cstdint>
#include "common/enum.h"

namespace kpengine::graphics
{


    class Shader
    {
    public:
        explicit Shader(ShaderType type, const std::string& path) : type_(type), path_(path) {}
        virtual ~Shader() = default;

        virtual const void* GetCode() const = 0;
        virtual size_t GetCodeSize() const = 0;

        ShaderType GetShaderType() const{return type_;}
        std::string GetPath() const{return path_;}
    protected:
        ShaderType type_;
        std::string path_;
    };

}

#endif