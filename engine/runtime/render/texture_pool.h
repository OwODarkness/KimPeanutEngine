#ifndef KPENGINE_RUNTIME_TEXTURE_POOL_H
#define KPENGINE_RUNTIME_TEXTURE_POOL_H

#include <memory>
#include <unordered_map>
#include <string>

namespace kpengine{
    class RenderTexture;

    class TexturePool{
    public:
        TexturePool() = default;
        ~TexturePool();
        bool IsTextureCached(const std::string& key) const;
        void AddTexture(std::shared_ptr<RenderTexture> texture);
        std::shared_ptr<RenderTexture> FindTextureByKey(const std::string& key) const;
        unsigned int FindHandleByTexture(const std::shared_ptr<RenderTexture>& texture) const;
        unsigned int FindHandleByKey(const std::string& key) const;
    private:
        std::unordered_map<std::string, std::shared_ptr<RenderTexture>> texture_map;
    };
}

#endif//KPENGINE_RUNTIME_TEXTURE_CACHE_H