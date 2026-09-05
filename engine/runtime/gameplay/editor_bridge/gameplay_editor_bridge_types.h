#ifndef KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_GAMEPLAY_EDITOR_BRIDGE_TYPES_H
#define KPENGINE_RUNTIME_GAMEPLAY_EDITOR_BRIDGE_GAMEPLAY_EDITOR_BRIDGE_TYPES_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gameplay/actor/actor_types.h"
#include "reflection/reflection_types.h"

namespace kpengine::gameplay
{
    struct SnapshotOmissionCounts
    {
        std::size_t actors = 0;
        std::size_t components = 0;
        std::size_t properties = 0;
        std::size_t value_bytes = 0;
    };

    struct PropertyValueSnapshot
    {
        reflection::ReflectionPropertyId property;
        reflection::ReflectionResultStatus status = reflection::ReflectionResultStatus::Success;
        reflection::ReflectionValue value;
        std::string diagnostic;
    };

    struct ComponentEditorSnapshot
    {
        ComponentInstanceId component;
        reflection::ReflectionTypeId type;
        bool is_root = false;
        std::vector<PropertyValueSnapshot> properties;
        std::string diagnostic;
    };

    struct ActorEditorSnapshot
    {
        ActorHandle actor;
        ActorState state = ActorState::Constructed;
        std::string display_name;
        std::optional<ComponentInstanceId> root_component;
        std::vector<ComponentEditorSnapshot> components;
    };

    struct GameplayEditorSnapshot
    {
        uint64_t revision = 0;
        bool truncated = false;
        SnapshotOmissionCounts omitted;
        std::vector<ActorEditorSnapshot> actors;
    };

    struct GameplayEditorBridgeConfig
    {
        std::size_t max_actors = 1024;
        std::size_t max_components = 4096;
        std::size_t max_properties = 32768;
        std::size_t max_value_bytes = 4 * 1024 * 1024;
        std::size_t max_outstanding_edits = 256;
    };

    struct PropertyEditCommand
    {
        uint64_t request_id = 0;
        ActorHandle actor;
        ComponentInstanceId component;
        reflection::ReflectionTypeId expected_type;
        reflection::ReflectionPropertyId property;
        reflection::ReflectionValue value;
    };

    enum class PropertyEditSubmissionStatus : uint8_t
    {
        Queued,
        QueueFull,
        Stopped,
        InvalidArgument,
    };

    struct PropertyEditSubmission
    {
        PropertyEditSubmissionStatus status = PropertyEditSubmissionStatus::InvalidArgument;
        uint64_t request_id = 0;
        std::string diagnostic;

        bool IsQueued() const noexcept
        {
            return status == PropertyEditSubmissionStatus::Queued;
        }
    };

    enum class PropertyEditResultStatus : uint8_t
    {
        Applied,
        StaleActor,
        StaleComponent,
        ReflectedTypeMismatch,
        UnknownProperty,
        NotWritable,
        ValueRejected,
        ReadbackFailed,
        CancelledByShutdown,
    };

    struct PropertyEditResult
    {
        uint64_t request_id = 0;
        PropertyEditResultStatus status = PropertyEditResultStatus::ValueRejected;
        reflection::ReflectionResultStatus reflection_status =
            reflection::ReflectionResultStatus::Success;
        reflection::ReflectionValue value;
        std::string diagnostic;
    };
}

#endif
