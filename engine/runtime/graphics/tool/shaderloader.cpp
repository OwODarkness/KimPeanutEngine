#include "shaderloader.h"

#include <fstream>

namespace kpengine::graphics
{

    std::vector<char> ShaderLoader::ReadTextFile(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open text file");
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::vector<char> ShaderLoader::ReadBinaryFile(const std::string &path)
    {
        std::ifstream file(path, std::ios_base::ate | std::ios_base::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open text file");
        }
        size_t file_size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(file_size);
        file.seekg(0);
        file.read(buffer.data(), file_size);
        return buffer;
    }

}