#include "level/level_instance.h"

#include <unordered_set>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::runtime
{
    namespace
    {
        template <typename Callback>
        class ScopeGuard final
        {
        public:
            explicit ScopeGuard(Callback callback) : callback_(std::move(callback)) {}

            ~ScopeGuard() noexcept
            {
                if (active_)
                {
                    callback_();
                }
            }

            void Dismiss() noexcept { active_ = false; }

        private:
            Callback callback_;
            bool active_ = true;
        };

        LevelInstanceResult Failure(LevelInstanceError error, std::string diagnostic)
        {
            return {error, std::move(diagnostic)};
        }

        Transform3f ToGameplayTransform(const asset::LevelTransform &transform)
        {
            return {transform.position,
                    {transform.rotation_degrees.x_, transform.rotation_degrees.y_,
                     transform.rotation_degrees.z_},
                    transform.scale};
        }
    }

    LevelInstance::LevelInstance(asset::AssetManager &asset_manager,
                                 gameplay::GameplayWorld &gameplay_world,
                                 StaticMeshActorFactory actor_factory)
        : asset_manager_(asset_manager), gameplay_world_(gameplay_world),
          actor_factory_(actor_factory ? std::move(actor_factory)
                                       : StaticMeshActorFactory(
                                             [](gameplay::GameplayWorld &world,
                                                const gameplay::StaticMeshActorDesc &description)
                                             {
                                                 return gameplay::CreateStaticMeshActor(world,
                                                                                        description);
                                             }))
    {
    }

    LevelInstance::~LevelInstance()
    {
        Unload();
    }

    LevelInstanceResult LevelInstance::Instantiate(const asset::AssetID &level_asset)
    {
        if (active_)
        {
            return Failure(LevelInstanceError::InvalidState,
                           "a level instance is already active");
        }
        if (!level_asset.IsValid() || level_asset.type != asset::AssetType::KPAT_Level)
        {
            return Failure(LevelInstanceError::InvalidLevelAsset,
                           "level asset is invalid or has the wrong type");
        }

        asset::Asset *const level_wrapper = asset_manager_.GetAsset(level_asset);
        const std::shared_ptr<asset::LevelResource> level_resource =
            level_wrapper != nullptr ? level_wrapper->GetResource<asset::LevelResource>() : nullptr;
        if (level_wrapper == nullptr || level_wrapper->GetType() != asset::AssetType::KPAT_Level ||
            level_resource == nullptr)
        {
            return Failure(LevelInstanceError::InvalidLevelAsset,
                           "level asset is stale, wrong-typed, or has no LevelResource");
        }

        // Phase A: copy and validate every static-mesh description before the
        // first Actor or render-source command can be created.
        std::vector<PendingActor> pending_actors;
        pending_actors.reserve(level_resource->objects.size());
        std::unordered_set<std::string> authored_ids;
        for (const asset::LevelObject &object : level_resource->objects)
        {
            const auto *const record = std::get_if<asset::LevelStaticMeshRecord>(&object);
            if (record == nullptr)
            {
                continue;
            }

            if (record->id.empty() || !authored_ids.emplace(record->id).second)
            {
                return Failure(LevelInstanceError::InvalidLevelAsset,
                               "duplicate or empty static-mesh authored ID: " + record->id);
            }

            PendingActor pending{};
            pending.authored_id = record->id;
            const LevelInstanceResult result =
                BuildStaticMeshDescription(level_asset, *record, pending.description);
            if (!result)
            {
                return result;
            }
            pending_actors.push_back(std::move(pending));
        }

        // Phase B: create in authored order, publishing instance state only
        // after the complete set has succeeded.
        std::vector<gameplay::ActorHandle> created_handles;
        created_handles.reserve(pending_actors.size());
        auto rollback = [this, &created_handles]() noexcept { Rollback(created_handles); };
        ScopeGuard rollback_guard{std::move(rollback)};
        for (const PendingActor &pending : pending_actors)
        {
            const gameplay::ActorHandle handle = actor_factory_(gameplay_world_, pending.description);
            if (!handle.IsValid() || gameplay_world_.FindActor(handle) == nullptr)
            {
                // A production factory may have created and then rejected an
                // intermediate Actor. Reclaim at the transaction boundary so
                // the failure cannot poison the next immediate retry.
                gameplay_world_.ReclaimDestroyedActors();
                return Failure(LevelInstanceError::ActorCreationFailed,
                               "could not create static-mesh Actor for authored ID: " +
                                   pending.authored_id);
            }
            created_handles.push_back(handle);
        }

        std::unordered_map<std::string, gameplay::ActorHandle> pending_actor_by_authored_id;
        pending_actor_by_authored_id.reserve(pending_actors.size());
        for (std::size_t index = 0; index < pending_actors.size(); ++index)
        {
            const auto [it, inserted] = pending_actor_by_authored_id.emplace(
                pending_actors[index].authored_id, created_handles[index]);
            if (!inserted)
            {
                return Failure(LevelInstanceError::InvalidLevelAsset,
                               "duplicate static-mesh authored ID during commit: " +
                                   pending_actors[index].authored_id);
            }
        }

        actor_by_authored_id_ = std::move(pending_actor_by_authored_id);
        creation_order_ = std::move(created_handles);
        active_level_asset_ = level_asset;
        active_ = true;
        rollback_guard.Dismiss();
        return {};
    }

    LevelInstanceResult LevelInstance::BuildStaticMeshDescription(
        const asset::AssetID &level_asset, const asset::LevelStaticMeshRecord &record,
        gameplay::StaticMeshActorDesc &description) const
    {
        const asset::AssetID model_asset = asset_manager_.ResolveDependency(
            level_asset, record.model.dependency_index, asset::AssetType::KPAT_Model);
        if (!model_asset.IsValid())
        {
            return Failure(LevelInstanceError::DependencyResolutionFailed,
                           "model dependency failed for authored ID: " + record.id);
        }

        asset::Asset *const model_wrapper = asset_manager_.GetAsset(model_asset);
        const std::shared_ptr<asset::ModelResource> model_resource =
            model_wrapper != nullptr ? model_wrapper->GetResource<asset::ModelResource>() : nullptr;
        if (model_wrapper == nullptr || model_wrapper->GetType() != asset::AssetType::KPAT_Model ||
            model_resource == nullptr)
        {
            return Failure(LevelInstanceError::InvalidModelResource,
                           "model dependency has no valid ModelResource for authored ID: " +
                               record.id);
        }

        const asset::AssetID mesh_asset = model_resource->GetData(asset::ModelGeometryType::KPMG_Mesh);
        if (!mesh_asset.IsValid())
        {
            return Failure(LevelInstanceError::MissingMeshGeometry,
                           "model has no KPMG_Mesh geometry for authored ID: " + record.id);
        }
        if (mesh_asset.type != asset::AssetType::KPAT_Mesh)
        {
            return Failure(LevelInstanceError::InvalidMeshAsset,
                           "model KPMG_Mesh geometry has the wrong type for authored ID: " +
                               record.id);
        }

        asset::Asset *const mesh_wrapper = asset_manager_.GetAsset(mesh_asset);
        const std::shared_ptr<asset::MeshResource> mesh_resource =
            mesh_wrapper != nullptr ? mesh_wrapper->GetResource<asset::MeshResource>() : nullptr;
        if (mesh_wrapper == nullptr || mesh_wrapper->GetType() != asset::AssetType::KPAT_Mesh)
        {
            return Failure(LevelInstanceError::InvalidMeshAsset,
                           "model KPMG_Mesh dependency is stale for authored ID: " + record.id);
        }
        if (mesh_resource == nullptr || mesh_resource->data == nullptr ||
            mesh_resource->data->vertices.empty() || mesh_resource->data->indices.empty() ||
            !mesh_resource->local_bounds.IsValid())
        {
            return Failure(LevelInstanceError::InvalidMeshData,
                           "mesh dependency has invalid geometry or bounds for authored ID: " +
                               record.id);
        }

        const asset::AssetID material_asset = asset_manager_.ResolveDependency(
            level_asset, record.material.dependency_index, asset::AssetType::KPAT_Material);
        if (!material_asset.IsValid())
        {
            return Failure(LevelInstanceError::DependencyResolutionFailed,
                           "material dependency failed for authored ID: " + record.id);
        }

        asset::Asset *const material_wrapper = asset_manager_.GetAsset(material_asset);
        const std::shared_ptr<asset::MaterialResource> material_resource =
            material_wrapper != nullptr
                ? material_wrapper->GetResource<asset::MaterialResource>()
                : nullptr;
        if (material_wrapper == nullptr ||
            material_wrapper->GetType() != asset::AssetType::KPAT_Material ||
            material_resource == nullptr)
        {
            return Failure(LevelInstanceError::InvalidMaterialResource,
                           "material dependency has no valid MaterialResource for authored ID: " +
                               record.id);
        }

        description.mesh_asset = mesh_asset;
        description.material_asset = material_asset;
        description.transform = ToGameplayTransform(record.transform);
        description.local_bounds = mesh_resource->local_bounds;
        description.visible = record.visible;
        description.casts_shadow = record.casts_shadow;
        description.lod_bias = record.lod_bias;
        return {};
    }

    void LevelInstance::Rollback(const std::vector<gameplay::ActorHandle> &created_handles)
    {
        for (auto it = created_handles.rbegin(); it != created_handles.rend(); ++it)
        {
            (void)gameplay_world_.DestroyActor(*it);
        }
        gameplay_world_.ReclaimDestroyedActors();
    }

    void LevelInstance::Unload()
    {
        if (!active_)
        {
            return;
        }

        for (auto it = creation_order_.rbegin(); it != creation_order_.rend(); ++it)
        {
            (void)gameplay_world_.DestroyActor(*it);
        }
        gameplay_world_.ReclaimDestroyedActors();

        actor_by_authored_id_.clear();
        creation_order_.clear();
        active_level_asset_ = {};
        active_ = false;
    }

    std::optional<gameplay::ActorHandle> LevelInstance::FindActor(
        const std::string &authored_id) const
    {
        const auto it = actor_by_authored_id_.find(authored_id);
        if (it == actor_by_authored_id_.end() || gameplay_world_.FindActor(it->second) == nullptr)
        {
            return std::nullopt;
        }
        return it->second;
    }
}
