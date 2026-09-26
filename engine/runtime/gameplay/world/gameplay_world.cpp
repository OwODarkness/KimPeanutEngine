#include "gameplay/world/gameplay_world.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/controller/player_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "spatial/ray.h"

namespace kpengine::gameplay
{
    namespace
    {
        bool ContainsNameAsciiCaseInsensitive(const std::string_view name,
                                              const std::string_view query)
        {
            if (query.empty())
            {
                return true;
            }
            if (query.size() > name.size())
            {
                return false;
            }

            return std::search(name.begin(), name.end(), query.begin(), query.end(),
                               [](const unsigned char left, const unsigned char right)
                               {
                                   return std::tolower(left) == std::tolower(right);
                               }) != name.end();
        }
    }

    GameplayWorld::GameplayWorld(render::IRenderableSourceSink *source_sink,
                                 render::ILightSourceSink *light_source_sink,
                                 render::ICameraSourceSink *camera_source_sink)
        : source_sink_(source_sink), light_source_sink_(light_source_sink),
          camera_source_sink_(camera_source_sink)
    {
    }

    GameplayWorld::~GameplayWorld()
    {
        Clear();
    }

    ActorHandle GameplayWorld::CreateActor()
    {
        const ActorHandle handle = actor_handles_.Create();
        actors_.emplace(handle.id,
                        std::make_unique<Actor>(handle, source_sink_, light_source_sink_,
                                                camera_source_sink_));
        return handle;
    }

    Actor *GameplayWorld::FindActor(ActorHandle handle)
    {
        if (!actor_handles_.IsHandleValid(handle))
        {
            return nullptr;
        }

        const auto it = actors_.find(handle.id);
        return it != actors_.end() && it->second->GetHandle() == handle &&
                       it->second->GetState() != ActorState::Destroyed
                   ? it->second.get()
                   : nullptr;
    }

    const Actor *GameplayWorld::FindActor(ActorHandle handle) const
    {
        if (!actor_handles_.IsHandleValid(handle))
        {
            return nullptr;
        }

        const auto it = actors_.find(handle.id);
        return it != actors_.end() && it->second->GetHandle() == handle &&
                       it->second->GetState() != ActorState::Destroyed
                   ? it->second.get()
                   : nullptr;
    }

    ActorListPage GameplayWorld::ListActors(const std::size_t offset,
                                            const std::size_t limit,
                                            const std::string_view name_contains) const
    {
        ActorListPage page;
        if (limit == 0)
        {
            return page;
        }

        std::vector<ActorSummary> summaries;
        summaries.reserve(actors_.size());
        for (const auto &[id, actor] : actors_)
        {
            (void)id;
            if (actor == nullptr || actor->GetState() == ActorState::Destroyed ||
                !ContainsNameAsciiCaseInsensitive(actor->GetName(), name_contains))
            {
                continue;
            }
            summaries.push_back(
                {actor->GetHandle(), actor->GetName(), actor->GetState(),
                 actor->GetRootComponent() != nullptr});
        }

        std::sort(summaries.begin(), summaries.end(),
                  [](const ActorSummary &left, const ActorSummary &right)
                  {
                      if (left.handle.id != right.handle.id)
                      {
                          return left.handle.id < right.handle.id;
                      }
                      return left.handle.generation < right.handle.generation;
                  });

        page.total_count = summaries.size();
        if (offset >= summaries.size())
        {
            return page;
        }

        const std::size_t available = summaries.size() - offset;
        const std::size_t count = std::min(limit, available);
        page.actors.insert(page.actors.end(), summaries.begin() + offset,
                           summaries.begin() + offset + count);
        page.has_more = count < available;
        return page;
    }

    std::optional<ActorQuery> GameplayWorld::QueryActor(const ActorHandle handle) const
    {
        const Actor *const actor = FindActor(handle);
        if (actor == nullptr)
        {
            return std::nullopt;
        }

        ActorQuery query;
        const SceneComponent *const root = actor->GetRootComponent();
        query.actor = {actor->GetHandle(), actor->GetName(), actor->GetState(), root != nullptr};
        if (root != nullptr)
        {
            query.root_local_transform = root->GetLocalTransform();
            query.root_world_transform = root->GetWorldTransform();
        }
        return query;
    }

    ActorTransformControlResult GameplayWorld::SetActorRootTransform(
        const ActorHandle handle, const Vector3f &local_position,
        const Rotatorf &local_rotation)
    {
        ActorTransformControlResult result;
        if (!std::isfinite(local_position.x_) || !std::isfinite(local_position.y_) ||
            !std::isfinite(local_position.z_) || !std::isfinite(local_rotation.pitch_) ||
            !std::isfinite(local_rotation.yaw_) || !std::isfinite(local_rotation.roll_))
        {
            result.status = ActorTransformControlStatus::InvalidTransform;
            return result;
        }

        Actor *const actor = FindActor(handle);
        if (actor == nullptr)
        {
            result.status = ActorTransformControlStatus::ActorUnavailable;
            return result;
        }

        SceneComponent *const root = actor->GetRootComponent();
        if (root == nullptr)
        {
            result.status = ActorTransformControlStatus::MissingRootComponent;
            return result;
        }

        Transform3f transform = root->GetLocalTransform();
        transform.position_ = local_position;
        transform.rotator_ = local_rotation;
        root->SetLocalTransform(transform);

        SynchronizeControlRotation(handle, *root);

        result.status = ActorTransformControlStatus::Applied;
        result.applied_transform = root->GetLocalTransform();
        return result;
    }

    void GameplayWorld::SynchronizeControlRotation(
        const ActorHandle handle, SceneComponent &changed_component)
    {
        if (local_player_controller_ != nullptr)
        {
            local_player_controller_->SynchronizeControlRotation(handle, changed_component);
        }
    }

    std::optional<ActorHandle> GameplayWorld::PickActor(const spatial::Ray &ray) const
    {
        std::optional<ActorHandle> closest_actor;
        std::optional<float> closest_distance;
        for (const auto &[id, actor] : actors_)
        {
            (void)id;
            if (actor == nullptr || actor->GetState() != ActorState::Active)
            {
                continue;
            }

            const MeshComponent *const mesh = actor->FindComponent<MeshComponent>();
            if (mesh == nullptr || !mesh->IsVisible())
            {
                continue;
            }

            const std::optional<float> distance =
                spatial::IntersectRayAABB(ray, mesh->GetWorldBounds());
            if (distance.has_value() &&
                (!closest_distance.has_value() || *distance < *closest_distance))
            {
                closest_distance = distance;
                closest_actor = actor->GetHandle();
            }
        }
        return closest_actor;
    }

    void GameplayWorld::SetSelectedActor(std::optional<ActorHandle> actor)
    {
        const Actor *const selected = actor.has_value() ? FindActor(*actor) : nullptr;
        const std::optional<ActorHandle> normalized_selection =
            selected != nullptr && selected->GetState() == ActorState::Active ? actor
                                                                                : std::nullopt;
        selected_actor_ = normalized_selection;

        for (auto &[id, candidate] : actors_)
        {
            (void)id;
            if (candidate == nullptr)
            {
                continue;
            }
            MeshComponent *const mesh = candidate->FindComponent<MeshComponent>();
            if (mesh != nullptr)
            {
                mesh->SetSelected(normalized_selection.has_value() &&
                                  candidate->GetHandle() == *normalized_selection);
            }
        }
    }

    bool GameplayWorld::InitializeActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Initialize();
    }

    bool GameplayWorld::ActivateActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Activate();
    }

    bool GameplayWorld::DeactivateActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Deactivate();
    }

    bool GameplayWorld::DestroyActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        if (!actor)
        {
            return false;
        }

        actor->Destroy();
        return true;
    }

    PlayerController *GameplayWorld::CreateLocalPlayerController(
        input::InputSystem *input_system, const std::string &input_context_name)
    {
        if (local_player_controller_ != nullptr)
        {
            return nullptr;
        }

        auto controller = std::make_unique<PlayerController>(*this, input_system,
                                                              input_context_name);
        if (input_system != nullptr && !controller->BindInput())
        {
            return nullptr;
        }
        local_player_controller_ = std::move(controller);
        return local_player_controller_.get();
    }

    void GameplayWorld::Tick(float delta_time)
    {
        if (local_player_controller_ != nullptr)
        {
            local_player_controller_->Tick(delta_time);
        }
        for (auto &[id, actor] : actors_)
        {
            (void)id;
            actor->Tick(delta_time);
        }
        ReclaimDestroyedActors();
    }

    void GameplayWorld::SetLocalPlayerControllerInputEnabled(bool enabled)
    {
        if (local_player_controller_ != nullptr)
        {
            local_player_controller_->SetInputEnabled(enabled);
        }
    }

    void GameplayWorld::Clear()
    {
        selected_actor_.reset();
        local_player_controller_.reset();
        for (auto &[id, actor] : actors_)
        {
            (void)id;
            actor->Destroy();
        }
        ReclaimDestroyedActors();
    }

    void GameplayWorld::ReclaimDestroyedActors()
    {
        for (auto it = actors_.begin(); it != actors_.end();)
        {
            if (it->second->GetState() == ActorState::Destroyed)
            {
                (void)actor_handles_.Destroy(it->second->GetHandle());
                it = actors_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
