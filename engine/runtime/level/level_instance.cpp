#include "level/level_instance.h"

#include <cmath>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/texture.h"
#include "gameplay/actor/actor.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/component/camera_component.h"
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

        render::CameraProjectionMode ToCameraProjection(asset::LevelProjection projection)
        {
            return projection == asset::LevelProjection::Orthographic
                       ? render::CameraProjectionMode::Orthographic
                       : render::CameraProjectionMode::Perspective;
        }
    }

    LevelInstance::LevelInstance(asset::AssetManager &asset_manager,
                                 gameplay::GameplayWorld &gameplay_world,
                                 LevelActorFactorySet factories,
                                 render::IEnvironmentSourceSink *environment_source_sink)
        : asset_manager_(asset_manager), gameplay_world_(gameplay_world),
          factories_(std::move(factories)), environment_source_sink_(environment_source_sink)
    {
        if (!factories_.static_mesh)
        {
            factories_.static_mesh = [](gameplay::GameplayWorld &world,
                                        const gameplay::StaticMeshActorDesc &description)
            { return gameplay::CreateStaticMeshActor(world, description); };
        }
        if (!factories_.directional_light)
        {
            factories_.directional_light = [](gameplay::GameplayWorld &world,
                                              const gameplay::DirectionalLightActorDesc &description)
            { return gameplay::CreateDirectionalLightActor(world, description); };
        }
        if (!factories_.point_light)
        {
            factories_.point_light = [](gameplay::GameplayWorld &world,
                                        const gameplay::PointLightActorDesc &description)
            { return gameplay::CreatePointLightActor(world, description); };
        }
        if (!factories_.spot_light)
        {
            factories_.spot_light = [](gameplay::GameplayWorld &world,
                                       const gameplay::SpotLightActorDesc &description)
            { return gameplay::CreateSpotLightActor(world, description); };
        }
        if (!factories_.camera)
        {
            factories_.camera = [](gameplay::GameplayWorld &world,
                                   const gameplay::CameraActorDesc &description)
            { return gameplay::CreateCameraActor(world, description); };
        }
    }

    LevelInstance::LevelInstance(asset::AssetManager &asset_manager,
                                 gameplay::GameplayWorld &gameplay_world,
                                 StaticMeshActorFactory static_mesh_factory)
        : LevelInstance(asset_manager, gameplay_world,
                        LevelActorFactorySet{std::move(static_mesh_factory), {}, {}, {}, {}},
                        nullptr)
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

        std::vector<PendingActor> pending_actors;
        pending_actors.reserve(level_resource->objects.size());
        std::unordered_set<std::string> authored_ids;
        for (const asset::LevelObject &object : level_resource->objects)
        {
            PendingActor pending{};
            const LevelInstanceResult result =
                BuildPendingActor(level_asset, object, pending, authored_ids);
            if (!result)
            {
                return result;
            }
            pending_actors.push_back(std::move(pending));
        }

        std::optional<render::EnvironmentSourceDesc> environment;
        if (level_resource->environment.has_value())
        {
            render::EnvironmentSourceDesc description{};
            const LevelInstanceResult result = BuildEnvironmentDescription(
                level_asset, *level_resource->environment, description);
            if (!result)
            {
                return result;
            }
            environment = description;
        }

        std::vector<gameplay::ActorHandle> created_handles;
        created_handles.reserve(pending_actors.size());
        render::EnvironmentSourceHandle created_environment_handle;
        auto rollback = [this, &created_handles, &created_environment_handle]() noexcept
        {
            if (created_environment_handle.IsValid() && environment_source_sink_ != nullptr)
            {
                (void)environment_source_sink_->EnqueueDestroy(created_environment_handle);
                created_environment_handle = {};
            }
            Rollback(created_handles);
        };
        ScopeGuard rollback_guard{std::move(rollback)};

        for (const PendingActor &pending : pending_actors)
        {
            const gameplay::ActorHandle handle = std::visit(
                [this](const auto &description)
                {
                    using Description = std::decay_t<decltype(description)>;
                    if constexpr (std::is_same_v<Description, gameplay::StaticMeshActorDesc>)
                    {
                        return factories_.static_mesh(gameplay_world_, description);
                    }
                    else if constexpr (std::is_same_v<Description,
                                                       gameplay::DirectionalLightActorDesc>)
                    {
                        return factories_.directional_light(gameplay_world_, description);
                    }
                    else if constexpr (std::is_same_v<Description,
                                                       gameplay::PointLightActorDesc>)
                    {
                        return factories_.point_light(gameplay_world_, description);
                    }
                    else if constexpr (std::is_same_v<Description, gameplay::SpotLightActorDesc>)
                    {
                        return factories_.spot_light(gameplay_world_, description);
                    }
                    else
                    {
                        return factories_.camera(gameplay_world_, description);
                    }
                },
                pending.description);
            if (!handle.IsValid() || gameplay_world_.FindActor(handle) == nullptr)
            {
                gameplay_world_.ReclaimDestroyedActors();
                return Failure(LevelInstanceError::ActorCreationFailed,
                               "could not create " + std::string(pending.kind) +
                                   " Actor for authored ID: " + pending.authored_id);
            }
            created_handles.push_back(handle);
        }

        if (environment.has_value())
        {
            if (environment_source_sink_ == nullptr)
            {
                return Failure(LevelInstanceError::EnvironmentSourceCreationFailed,
                               "level environment requires an environment source sink");
            }
            created_environment_handle =
                environment_source_sink_->EnqueueCreate(*environment);
            if (!created_environment_handle.IsValid())
            {
                return Failure(LevelInstanceError::EnvironmentSourceCreationFailed,
                               "environment source registration was rejected");
            }
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
                               "duplicate authored ID during commit: " +
                                   pending_actors[index].authored_id);
            }
        }

        actor_by_authored_id_ = std::move(pending_actor_by_authored_id);
        creation_order_ = std::move(created_handles);
        environment_source_handle_ = created_environment_handle;
        active_level_asset_ = level_asset;
        active_ = true;
        rollback_guard.Dismiss();
        return {};
    }

    LevelInstanceResult LevelInstance::BuildPendingActor(
        const asset::AssetID &level_asset, const asset::LevelObject &object, PendingActor &pending,
        std::unordered_set<std::string> &authored_ids) const
    {
        return std::visit(
            [this, &level_asset, &pending, &authored_ids](const auto &record)
                -> LevelInstanceResult
            {
                if (record.id.empty() || !authored_ids.emplace(record.id).second)
                {
                    return Failure(LevelInstanceError::InvalidLevelAsset,
                                   "duplicate or empty authored ID: " + record.id);
                }

                pending.authored_id = record.id;
                using Record = std::decay_t<decltype(record)>;
                if constexpr (std::is_same_v<Record, asset::LevelStaticMeshRecord>)
                {
                    pending.kind = "static-mesh";
                    gameplay::StaticMeshActorDesc description{};
                    const LevelInstanceResult result =
                        BuildStaticMeshDescription(level_asset, record, description);
                    if (!result)
                    {
                        return result;
                    }
                    pending.description = std::move(description);
                }
                else if constexpr (std::is_same_v<Record, asset::LevelDirectionalLightRecord>)
                {
                    pending.kind = "directional-light";
                    pending.description = gameplay::DirectionalLightActorDesc{
                        record.direction, record.color, record.intensity, record.enabled,
                        record.casts_shadow};
                }
                else if constexpr (std::is_same_v<Record, asset::LevelPointLightRecord>)
                {
                    pending.kind = "point-light";
                    pending.description = gameplay::PointLightActorDesc{
                        record.position, record.color, record.intensity, record.range,
                        record.enabled, record.casts_shadow};
                }
                else if constexpr (std::is_same_v<Record, asset::LevelSpotLightRecord>)
                {
                    pending.kind = "spot-light";
                    pending.description = gameplay::SpotLightActorDesc{
                        record.position, record.direction, record.color, record.intensity,
                        record.range, record.inner_cone_radians, record.outer_cone_radians,
                        record.enabled, record.casts_shadow};
                }
                else
                {
                    pending.kind = "camera";
                    pending.description = gameplay::CameraActorDesc{
                        ToGameplayTransform(record.transform), record.field_of_view_degrees,
                        record.near_plane, record.far_plane, record.orthographic_height,
                        ToCameraProjection(record.projection), record.enabled, record.priority};
                }
                return {};
            },
            object);
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

        const asset::AssetID mesh_asset =
            model_resource->GetData(asset::ModelGeometryType::KPMG_Mesh);
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

    LevelInstanceResult LevelInstance::BuildEnvironmentDescription(
        const asset::AssetID &level_asset, const asset::LevelEnvironmentRecord &record,
        render::EnvironmentSourceDesc &description) const
    {
        const asset::AssetID texture_asset = asset_manager_.ResolveDependency(
            level_asset, record.texture.dependency_index, asset::AssetType::KPAT_Texture);
        if (!texture_asset.IsValid())
        {
            return Failure(LevelInstanceError::DependencyResolutionFailed,
                           "environment texture dependency failed");
        }
        asset::Asset *const texture_wrapper = asset_manager_.GetAsset(texture_asset);
        const std::shared_ptr<asset::TextureResource> texture_resource =
            texture_wrapper != nullptr
                ? texture_wrapper->GetResource<asset::TextureResource>()
                : nullptr;
        if (texture_wrapper == nullptr || texture_wrapper->GetType() != asset::AssetType::KPAT_Texture ||
            texture_resource == nullptr || texture_resource->data == nullptr ||
            !std::isfinite(record.ibl_intensity) || record.ibl_intensity < 0.0f)
        {
            return Failure(LevelInstanceError::InvalidEnvironmentResource,
                           "environment texture has no valid CPU payload");
        }

        description.texture_asset = texture_asset;
        description.ibl_intensity = record.ibl_intensity;
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

        if (environment_source_handle_.IsValid() && environment_source_sink_ != nullptr)
        {
            (void)environment_source_sink_->EnqueueDestroy(environment_source_handle_);
            environment_source_handle_ = {};
        }
        for (auto it = creation_order_.rbegin(); it != creation_order_.rend(); ++it)
        {
            (void)gameplay_world_.DestroyActor(*it);
        }
        gameplay_world_.ReclaimDestroyedActors();

        actor_by_authored_id_.clear();
        creation_order_.clear();
        environment_source_handle_ = {};
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

    std::optional<gameplay::ActorHandle> LevelInstance::GetPreferredCameraActor() const
    {
        std::optional<gameplay::ActorHandle> selected;
        int selected_priority = 0;
        std::size_t selected_order = 0;
        bool has_selected = false;
        for (std::size_t order = 0; order < creation_order_.size(); ++order)
        {
            const gameplay::ActorHandle handle = creation_order_[order];
            const gameplay::Actor *const actor = gameplay_world_.FindActor(handle);
            const gameplay::CameraComponent *const camera =
                actor != nullptr ? actor->FindComponent<gameplay::CameraComponent>() : nullptr;
            if (camera == nullptr || !camera->IsCameraEnabled())
            {
                continue;
            }
            if (!has_selected || render::IsCameraPreferred(
                                    camera->GetPriority(), static_cast<uint32_t>(order),
                                    selected_priority, static_cast<uint32_t>(selected_order)))
            {
                selected = handle;
                selected_priority = camera->GetPriority();
                selected_order = order;
                has_selected = true;
            }
        }
        return selected;
    }
}
