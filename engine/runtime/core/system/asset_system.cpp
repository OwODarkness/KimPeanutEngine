#include "asset_system.h"
#include "runtime/render/texture_pool.h"
namespace kpengine{
    AssetSystem::AssetSystem():
    texture_pool_(nullptr)
    {
        
    }

    void AssetSystem::Initialize()
    {
        texture_pool_ = std::make_unique<TexturePool>();
        
    }

    AssetSystem::~AssetSystem() = default;
}