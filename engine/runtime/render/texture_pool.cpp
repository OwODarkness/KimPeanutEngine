#include "texture_pool.h"
#include "render_texture.h"
namespace kpengine{
    bool TexturePool::IsTextureCached(const std::string& key) const
    {
        return texture_map.contains(key);
    }

    std::shared_ptr<RenderTexture> TexturePool::FetchTexture2D(const std::string& path, bool is_texture_color)
    {
        if(IsTextureCached(path))
        {
            return FindTextureByKey(path);
        }
        else
        {
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>(path, is_texture_color);
            bool is_succeed = texture->Initialize();
            if(is_succeed)
            {
                AddTexture(texture);
                return texture;
            }
            else
            {
                return nullptr;
            }
        }
    }

    void TexturePool::AddTexture(std::shared_ptr<RenderTexture> texture)
    {
        if(!texture)
        {
            return ;
        }
        texture_map.insert({texture->image_id_, texture});
    }


    std::shared_ptr<RenderTexture> TexturePool::FindTextureByKey(const std::string& key) const
    {
        if(!texture_map.contains(key))
        {
            return nullptr;
        }
        return texture_map.at(key);    
    }
    unsigned int TexturePool::FindHandleByTexture(const std::shared_ptr<RenderTexture>& texture) const
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
    unsigned int TexturePool::FindHandleByKey(const std::string& key) const
    {
        if(!texture_map.contains(key))
        {
            return 0;
        }
        return texture_map.at(key)->GetTexture();
    }

    TexturePool::~TexturePool() = default;
}