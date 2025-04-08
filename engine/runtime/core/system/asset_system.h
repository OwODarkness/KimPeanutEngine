#ifndef KPENGINE_RUNTIME_ASSET_SYSTEM_H
#define KPENGINE_RUNTIME_ASSET_SYSTEM_H

#include <memory>

namespace kpengine{
    class TexturePool;

    class AssetSystem{
    public:
        AssetSystem();
        void Initialize();
        TexturePool* GetTexturePool(){return texture_pool_.get();}
        ~AssetSystem();
    public:
        std::unique_ptr<TexturePool> texture_pool_;
    };
}

#endif// KPENGINE_RUNTIME_ASSET_SYSTEM_H