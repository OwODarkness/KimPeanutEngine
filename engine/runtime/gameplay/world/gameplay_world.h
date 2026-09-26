#ifndef KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H
#define KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/handle.h"
#include "gameplay/actor/actor.h"
#include "gameplay/actor/actor_types.h"
#include "math/math_header.h"
#include "spatial/ray.h"

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine::render
{
    class ICameraSourceSink;
    class ILightSourceSink;
    class IRenderableSourceSink;
}

namespace kpengine::gameplay
{
    struct ActorSummary
    {
        ActorHandle handle;
        std::string name;
        ActorState state = ActorState::Constructed;
        bool has_root_component = false;
    };

    struct ActorListPage
    {
        std::vector<ActorSummary> actors;
        std::size_t total_count = 0;
        bool has_more = false;
    };

    struct ActorQuery
    {
        ActorSummary actor;
        std::optional<Transform3f> root_local_transform;
        std::optional<Transform3f> root_world_transform;
    };

    enum class ActorTransformControlStatus : uint8_t
    {
        Applied,
        ActorUnavailable,
        MissingRootComponent,
        InvalidTransform,
    };

    struct ActorTransformControlResult
    {
        ActorTransformControlStatus status = ActorTransformControlStatus::ActorUnavailable;
        Transform3f applied_transform;

        bool IsSuccess() const noexcept
        {
            return status == ActorTransformControlStatus::Applied;
        }
    };

    class PlayerController;
    class GameplayEditorBridge;

    class GameplayWorld
    {
    public:
        explicit GameplayWorld(render::IRenderableSourceSink *source_sink = nullptr,
                               render::ILightSourceSink *light_source_sink = nullptr,
                               render::ICameraSourceSink *camera_source_sink = nullptr);
        ~GameplayWorld();

        GameplayWorld(const GameplayWorld &) = delete;
        GameplayWorld &operator=(const GameplayWorld &) = delete;
        GameplayWorld(GameplayWorld &&) = delete;
        GameplayWorld &operator=(GameplayWorld &&) = delete;

        ActorHandle CreateActor();
        Actor *FindActor(ActorHandle handle);
        const Actor *FindActor(ActorHandle handle) const;
        ActorListPage ListActors(std::size_t offset, std::size_t limit,
                                 std::string_view name_contains = {}) const;
        std::optional<ActorQuery> QueryActor(ActorHandle handle) const;
        ActorTransformControlResult SetActorRootTransform(
            ActorHandle handle, const Vector3f &local_position,
            const Rotatorf &local_rotation);
        std::optional<ActorHandle> PickActor(const spatial::Ray &ray) const;
        void SetSelectedActor(std::optional<ActorHandle> actor);

        bool InitializeActor(ActorHandle handle);
        bool ActivateActor(ActorHandle handle);
        bool DeactivateActor(ActorHandle handle);
        bool DestroyActor(ActorHandle handle);

        PlayerController *CreateLocalPlayerController(input::InputSystem *input_system,
                                                      const std::string &input_context_name =
                                                          "Gameplay");
        PlayerController *GetLocalPlayerController() const
        {
            return local_player_controller_.get();
        }

        // Applied on the game thread by Runtime's camera-control boundary.
        void SetLocalPlayerControllerInputEnabled(bool enabled);

        void Tick(float delta_time);
        void Clear();

        // Game-thread lifecycle boundary for transactional rollback/unload.
        // DestroyActor invalidates lookup immediately, while this operation
        // releases the owned Actor storage and handle slot. Do not call it
        // from inside world iteration.
        void ReclaimDestroyedActors();

    private:
        friend class GameplayEditorBridge;

        HandleSystem<ActorHandle> actor_handles_;
        std::unordered_map<uint32_t, std::unique_ptr<Actor>> actors_;
        std::optional<ActorHandle> selected_actor_;
        std::unique_ptr<PlayerController> local_player_controller_;
        render::IRenderableSourceSink *source_sink_ = nullptr;
        render::ILightSourceSink *light_source_sink_ = nullptr;
        render::ICameraSourceSink *camera_source_sink_ = nullptr;

        void SynchronizeControlRotation(ActorHandle handle, SceneComponent &changed_component);
    };
}

#endif
