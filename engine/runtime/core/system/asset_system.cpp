#include "asset_system.h"
#include "runtime/render/texture_cache.h"
namespace kpengine{
    AssetSystem::AssetSystem():
    texture_cache_(nullptr)
    {
        
    }

    void AssetSystem::Initialize()
    {
        texture_cache_ = std::make_unique<TextureCache>();
    }
}