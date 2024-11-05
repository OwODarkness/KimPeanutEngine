#ifndef KPENGINE_RUNTIME_RENDER_TEXTURE_H
#define KPENGINE_RUNTIME_RENDER_TEXTURE_H

#include <string>
#include <filesystem>
namespace kpengine{
    const std::filesystem::path texture_directory_path = "../../engine/asset/texture";

class RenderTexture{
public:
    explicit RenderTexture(const std::string& image_path);

    void Initialize();

    inline unsigned int GetTexture() const {return texture_handle_;}
private:
    std::string image_path_;
    unsigned int texture_handle_;
};
}

#endif