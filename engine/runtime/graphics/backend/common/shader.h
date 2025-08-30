#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_H

#include <string>
#include "common/enum.h"

namespace kpengine::graphics
{
    class Shader
    {
    public:
        explicit Shader(const std::string &name, ShaderType type) : name_(name), type_(type) {}
        virtual ~Shader() = default;

        const std::string &GetName() const { return name_; }

    protected:
        std::string name_;
        ShaderType type_;
    };


}

#endif