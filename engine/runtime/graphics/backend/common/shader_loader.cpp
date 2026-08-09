#include "shader_loader.h"
#include "log/logger.h"
#include <fstream>

namespace kpengine::graphics
{

    std::vector<char> ShaderLoader::ReadTextFile(const std::string &path)
    {
        std::ifstream file(path, std::ios_base::ate);
        if (!file.is_open())
        {
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_ERROR, "failed to open text file: " + path);
        }

        size_t file_size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<char> buffer(file_size);
        file.read(buffer.data(), file_size);
        return buffer;
    }

    std::vector<uint32_t> ShaderLoader::ReadBinaryFile(const std::string &path)
    {
        std::ifstream file(path, std::ios_base::ate | std::ios_base::binary);
        if (!file.is_open())
        {
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_ERROR, "failed to open binary file: " + path);
            throw std::runtime_error("Failed to find binary file: "+ path);
        }

        size_t file_size = static_cast<size_t>(file.tellg());
        if (file_size % 4 != 0)
        {
            std::string msg = "failed to open binary file: " + path;
            KP_LOG("ShaderLoaderLog", LOG_LEVEL_ERROR, msg);
            throw std::runtime_error(msg);
        }

        std::vector<uint32_t> buffer(file_size / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), file_size);

        return buffer;
    }

}