#ifndef KPENGINE_RUNTIME_LEVEL_LEVEL_INSTANCE_H
#define KPENGINE_RUNTIME_LEVEL_LEVEL_INSTANCE_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset/asset.h"
#include "gameplay/actor/actor_types.h"
#include "gameplay/factory/static_mesh_actor_factory.h"

namespace kpengine::asset
{
    class AssetManager;
    struct LevelStaticMeshRecord;
}

namespace kpengine::gameplay
{
    class GameplayWorld;
}

namespace kpengine::runtime
{
    enum class LevelInstanceError
    {
        None,
        InvalidState,
        InvalidLevelAsset,
        DependencyResolutionFailed,
        InvalidModelResource,
        MissingMeshGeometry,
        InvalidMeshAsset,
        InvalidMeshData,
        InvalidMaterialResource,
        ActorCreationFailed,
    };

    struct LevelInstanceResult
    {
        LevelInstanceError error = LevelInstanceError::None;
        std::string diagnostic;

        bool IsSuccess() const { return error == LevelInstanceError::None; }
        explicit operator bool() const { return IsSuccess(); }
    };

    using StaticMeshActorFactory = std::function<gameplay::ActorHandle(
        gameplay::GameplayWorld &, const gameplay::StaticMeshActorDesc &)>;

    class LevelInstance final
    {
    public:
        LevelInstance(asset::AssetManager &asset_manager, gameplay::GameplayWorld &gameplay_world,
                      StaticMeshActorFactory actor_factory = {});
        ~LevelInstance();

        LevelInstance(const LevelInstance &) = delete;
        LevelInstance &operator=(const LevelInstance &) = delete;
        LevelInstance(LevelInstance &&) = delete;
        LevelInstance &operator=(LevelInstance &&) = delete;

        LevelInstanceResult Instantiate(const asset::AssetID &level_asset);
        void Unload();

        bool IsActive() const { return active_; }
        asset::AssetID GetLevelAsset() const { return active_level_asset_; }
        std::size_t GetActorCount() const { return actor_by_authored_id_.size(); }
        std::optional<gameplay::ActorHandle> FindActor(const std::string &authored_id) const;

    private:
        struct PendingActor
        {
            std::string authored_id;
            gameplay::StaticMeshActorDesc description;
        };

        LevelInstanceResult BuildStaticMeshDescription(
            const asset::AssetID &level_asset,
            const asset::LevelStaticMeshRecord &record,
            gameplay::StaticMeshActorDesc &description) const;
        void Rollback(const std::vector<gameplay::ActorHandle> &created_handles);

        asset::AssetManager &asset_manager_;
        gameplay::GameplayWorld &gameplay_world_;
        StaticMeshActorFactory actor_factory_;

        bool active_ = false;
        asset::AssetID active_level_asset_;
        std::unordered_map<std::string, gameplay::ActorHandle> actor_by_authored_id_;
        std::vector<gameplay::ActorHandle> creation_order_;
    };
}

#endif
