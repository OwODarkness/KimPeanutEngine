#include "texture_cache.h"
#include "render_texture.h"
namespace kpengine{
    bool TextureCache::IsTextureCached(const std::string& key) const
    {
        return texture_map.contains(key);
    }

    void TextureCache::AddTexture(std::shared_ptr<RenderTexture> texture)
    {
        if(!texture)
        {
            return ;
        }
        texture_map.insert({texture->image_id_, texture});
    }


    std::shared_ptr<RenderTexture> TextureCache::FindTextureByKey(const std::string& key) const
    {
        if(!texture_map.contains(key))
        {
            return nullptr;
        }
        return texture_map.at(key);    
    }
    unsigned int TextureCache::FindHandleByTexture(const std::shared_ptr<RenderTexture>& texture) const
    {
        for(const auto& pair : texture_map)
        {
            if(pair.second == texture)
            {
                return pair.second->GetTexture();
            }
        }
        return 0;
    }
    unsigned int TextureCache::FindHandleByKey(const std::string& key) const
    {
        if(!texture_map.contains(key))
        {
            return 0;
        }
        return texture_map.at(key)->GetTexture();
    }

    TextureCache::~TextureCache() = default;
}