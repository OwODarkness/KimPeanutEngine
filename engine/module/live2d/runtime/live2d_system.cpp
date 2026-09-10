#include "live2d_system.h"

#include <utility>

#include "asset/asset_manager.h"
#include "asset/texture.h"
#include "live2d_model_resource.h"

namespace kpengine::live2d
{
    Live2DSystem::~Live2DSystem() noexcept
    {
        Shutdown();
    }

    bool Live2DSystem::Initialize()
    {
        return cubism_.Initialize();
    }

    void Live2DSystem::Tick(float delta_time)
    {
        // Keep the service on the engine's game-thread schedule. Model update
        // and render submission will be added here once runtime assets exist.
        (void)delta_time;
    }

    void Live2DSystem::Shutdown() noexcept
    {
        cubism_.Shutdown();
    }

    bool Live2DSystem::IsInitialized() const noexcept
    {
        return cubism_.IsInitialized();
    }

    std::unique_ptr<Live2DModelInstance> Live2DSystem::CreateInstance(
        const asset::AssetID &asset_id)
    {
        if (!cubism_.IsInitialized() || !asset_id.IsValid() ||
            asset_id.type != kLive2DModelAssetType)
        {
            return nullptr;
        }

        asset::AssetManager &manager = asset::AssetManager::GetInstance();
        asset::Asset *asset = manager.GetAsset(asset_id);
        if (asset == nullptr)
        {
            return nullptr;
        }
        const std::shared_ptr<Live2DModelResource> resource =
            asset->GetResource<Live2DModelResource>();
        if (resource == nullptr)
        {
            return nullptr;
        }

        const std::vector<asset::AssetID> dependencies =
            asset->GetDependencies();
        if (dependencies.size() != resource->Product().textures.size())
        {
            return nullptr;
        }
        std::vector<std::shared_ptr<const asset::TextureResource>> textures;
        textures.reserve(dependencies.size());
        for (const asset::AssetID &dependency : dependencies)
        {
            if (!dependency.IsValid() ||
                dependency.type != asset::AssetType::KPAT_Texture)
            {
                return nullptr;
            }
            const std::shared_ptr<asset::TextureResource> texture =
                manager.GetResource<asset::TextureResource>(dependency);
            if (texture == nullptr || texture->data == nullptr)
            {
                return nullptr;
            }
            textures.push_back(std::shared_ptr<const asset::TextureResource>(
                std::move(texture)));
        }
        return Live2DModelInstance::Create(
            std::shared_ptr<const Live2DModelResource>(std::move(resource)),
            std::move(textures), cubism_);
    }
}
