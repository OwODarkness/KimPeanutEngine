#include "live2d_system.h"

#include <limits>
#include <optional>
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

    std::optional<std::uint64_t> Live2DSystem::AllocateInstanceSerial() noexcept
    {
        if (next_instance_serial_ == 0u)
        {
            return std::nullopt;
        }
        const std::uint64_t serial = next_instance_serial_;
        if (serial == std::numeric_limits<std::uint64_t>::max())
        {
            next_instance_serial_ = 0u;
        }
        else
        {
            ++next_instance_serial_;
        }
        return serial;
    }

    std::unique_ptr<Live2DModelInstance> Live2DSystem::CreateInstance(
        const asset::AssetID &asset_id)
    {
        return CreateInstance(asset_id, Live2DSecondaryBehaviorConfig{});
    }

    std::unique_ptr<Live2DModelInstance> Live2DSystem::CreateInstance(
        const asset::AssetID &asset_id,
        const Live2DSecondaryBehaviorConfig &behavior_config)
    {
        std::string diagnostic;
        return CreateInstance(asset_id, behavior_config, diagnostic);
    }

    std::unique_ptr<Live2DModelInstance> Live2DSystem::CreateInstance(
        const asset::AssetID &asset_id,
        const Live2DSecondaryBehaviorConfig &behavior_config,
        std::string &diagnostic)
    {
        diagnostic.clear();
        if (!cubism_.IsInitialized() || !asset_id.IsValid() ||
            asset_id.type != kLive2DModelAssetType)
        {
            diagnostic = "Live2D system or asset ID is invalid";
            return nullptr;
        }

        asset::AssetManager &manager = asset::AssetManager::GetInstance();
        asset::Asset *asset = manager.GetAsset(asset_id);
        if (asset == nullptr)
        {
            diagnostic = "Live2D asset was not found";
            return nullptr;
        }
        const std::shared_ptr<Live2DModelResource> resource =
            asset->GetResource<Live2DModelResource>();
        if (resource == nullptr)
        {
            diagnostic = "Live2D asset resource is unavailable";
            return nullptr;
        }

        const std::vector<asset::AssetID> dependencies = asset->GetDependencies();
        if (dependencies.size() != resource->Product().textures.size())
        {
            diagnostic = "Live2D texture dependency count does not match the product";
            return nullptr;
        }
        std::vector<std::shared_ptr<const asset::TextureResource>> textures;
        textures.reserve(dependencies.size());
        for (const asset::AssetID &dependency : dependencies)
        {
            if (!dependency.IsValid() ||
                dependency.type != asset::AssetType::KPAT_Texture)
            {
                diagnostic = "Live2D texture dependency is invalid";
                return nullptr;
            }
            const std::shared_ptr<asset::TextureResource> texture =
                manager.GetResource<asset::TextureResource>(dependency);
            if (texture == nullptr || texture->data == nullptr)
            {
                diagnostic = "Live2D texture dependency resource is unavailable";
                return nullptr;
            }
            textures.push_back(std::shared_ptr<const asset::TextureResource>(
                std::move(texture)));
        }
        const std::optional<std::uint64_t> instance_serial =
            AllocateInstanceSerial();
        if (!instance_serial.has_value())
        {
            diagnostic = "Live2D instance serial space is exhausted";
            return nullptr;
        }
        return Live2DModelInstance::Create(
            std::shared_ptr<const Live2DModelResource>(std::move(resource)),
            std::move(textures), *instance_serial, behavior_config, cubism_,
            diagnostic);
    }
}
