#include "shader_loader.h"

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

    std::vector<uint32_t> ShaderLoader::ReadBinaryFile(const std::string &path)
    {
        std::ifstream file(path, std::ios_base::ate | std::ios_base::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open binary file: " + path);
        }

        size_t file_size = static_cast<size_t>(file.tellg());
        if (file_size % 4 != 0)
        {
            throw std::runtime_error("SPIR-V file size is not a multiple of 4: " + path);
        }

        std::vector<uint32_t> buffer(file_size / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), file_size);

        return buffer;
    }

}