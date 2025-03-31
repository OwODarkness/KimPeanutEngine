#ifndef KPENGINE_RUNTIME_ASSET_SYSTEM_H
#define KPENGINE_RUNTIME_ASSET_SYSTEM_H

#include <memory>

namespace kpengine{
    class TextureCache;

    class AssetSystem{
    public:
        AssetSystem();
        void Initialize();
    public:
        std::unique_ptr<TextureCache> texture_cache_;
    };
}

#endif// KPENGINE_RUNTIME_ASSET_SYSTEM_H