#ifndef KPENGINE_RUNTIME_GRAPHICS_SHADER_LOADER_H
#define KPENGINE_RUNTIME_GRAPHICS_SHADER_LOADER_H

#include <vector>
#include <string>

namespace kpengine::graphics
{
    class ShaderLoader
    {
    public:
        static std::vector<char> ReadTextFile(const std::string &path);
        static std::vector<char> ReadBinaryFile(const std::string &path);
    };
}

#endif