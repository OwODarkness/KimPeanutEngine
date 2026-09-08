#include "live2d_system.h"

#include <utility>

#include "asset/asset_manager.h"
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

        const std::shared_ptr<Live2DModelResource> resource =
            asset::AssetManager::GetInstance().GetResource<Live2DModelResource>(
                asset_id);
        if (resource == nullptr)
        {
            return nullptr;
        }
        return Live2DModelInstance::Create(
            std::shared_ptr<const Live2DModelResource>(std::move(resource)),
            cubism_);
    }
}
