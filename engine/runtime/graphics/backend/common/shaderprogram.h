#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_PROGRAM_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_PROGRAM_H

#include <string>

namespace kpengine::graphics
{
    class ShaderProgram
    {
    public:
        explicit ShaderProgram(const std::string &name) : name_(name) {}
        virtual ~ShaderProgram() = default;
        virtual void Bind() = 0;
        virtual void Unbind() = 0;
        virtual void Cleanup() = 0;

        const std::string &GetName() const { return name_; }

    protected:
        std::string name_;
    };


}

#endif