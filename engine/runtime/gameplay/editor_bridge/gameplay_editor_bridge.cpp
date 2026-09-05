#include "gameplay/editor_bridge/gameplay_editor_bridge.h"

#include <algorithm>
#include <limits>
#include <string>
#include <typeinfo>

#include "gameplay/actor/actor.h"
#include "gameplay/component/actor_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/reflection/gameplay_reflection.h"
#include "gameplay/world/gameplay_world.h"
#include "reflection/i_reflection_access.h"
#include "reflection/i_reflection_catalog.h"

namespace kpengine::gameplay
{
    namespace
    {
        reflection::ReflectionResult MakeFailure(reflection::ReflectionResultStatus status,
                                                  const char *diagnostic)
        {
            return {status, diagnostic};
        }

        bool AddWithinBudget(std::size_t current, std::size_t added, std::size_t maximum) noexcept
        {
            return added <= maximum && current <= maximum - added;
        }

        void SaturatingAdd(std::size_t &value, std::size_t added) noexcept
        {
            const std::size_t maximum = std::numeric_limits<std::size_t>::max();
            value = added > maximum - value ? maximum : value + added;
        }
    }

    GameplayEditorBridge::GameplayEditorBridge(
        GameplayWorld &world,
        const reflection::IReflectionCatalog &catalog,
        const reflection::IReflectionAccess &access,
        GameplayEditorBridgeConfig config)
        : world_(world), catalog_(catalog), access_(access), config_(config)
    {
    }

    GameplayEditorBridge::~GameplayEditorBridge()
    {
        Shutdown();
    }

    reflection::ReflectionResult GameplayEditorBridge::Initialize()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Constructed)
        {
            return MakeFailure(reflection::ReflectionResultStatus::AlreadyInitialized,
                               "Gameplay editor bridge is already initialized");
        }
        if (config_.max_actors == 0 || config_.max_components == 0 ||
            config_.max_properties == 0 || config_.max_value_bytes == 0 ||
            config_.max_outstanding_edits == 0)
        {
            return MakeFailure(reflection::ReflectionResultStatus::InvalidArgument,
                               "Gameplay editor bridge budgets must be nonzero");
        }

        const std::vector<GameplayReflectionBinding> declared_bindings =
            GetGameplayReflectionBindings();
        std::unordered_set<std::string> names;
        std::unordered_set<uint32_t> types;
        bindings_.reserve(declared_bindings.size());
        for (const GameplayReflectionBinding &binding : declared_bindings)
        {
            if (binding.canonical_name.empty() || binding.matches == nullptr ||
                binding.make_const_object == nullptr || binding.make_mutable_object == nullptr)
            {
                return MakeFailure(reflection::ReflectionResultStatus::InvalidDescriptor,
                                   "Gameplay reflection binding is incomplete");
            }
            if (!names.insert(binding.canonical_name).second)
            {
                return MakeFailure(reflection::ReflectionResultStatus::DuplicateName,
                                   "Gameplay reflection binding names must be unique");
            }

            const reflection::ReflectionTypeDescriptor *const descriptor =
                catalog_.FindType(binding.canonical_name);
            if (descriptor == nullptr || !descriptor->id.IsValid() ||
                descriptor->name != binding.canonical_name)
            {
                return MakeFailure(reflection::ReflectionResultStatus::InvalidDescriptor,
                                   "Gameplay reflection binding does not match its catalog type");
            }
            if (!types.insert(descriptor->id.value).second)
            {
                return MakeFailure(reflection::ReflectionResultStatus::IdCollision,
                                   "Gameplay reflection bindings resolve to duplicate types");
            }
            bindings_.push_back({binding, descriptor->id});
        }

        for (const reflection::ReflectionTypeDescriptor &descriptor :
             catalog_.EnumerateTypes())
        {
            const std::size_t matching_bindings = static_cast<std::size_t>(std::count_if(
                bindings_.begin(), bindings_.end(), [&descriptor](const ResolvedBinding &binding) {
                    return binding.type == descriptor.id &&
                           binding.binding.canonical_name == descriptor.name;
                }));
            if (matching_bindings != 1)
            {
                return MakeFailure(reflection::ReflectionResultStatus::InvalidDescriptor,
                                   "Gameplay reflection catalog and binding manifest disagree");
            }
        }

        game_thread_id_ = std::this_thread::get_id();
        state_ = State::Running;
        return {};
    }

    GameplayEditorBridge::State GameplayEditorBridge::GetState() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    std::shared_ptr<const GameplayEditorSnapshot>
    GameplayEditorBridge::GetLatestSnapshot() const
    {
        return std::atomic_load_explicit(&latest_snapshot_, std::memory_order_acquire);
    }

    std::vector<PropertyEditResult> GameplayEditorBridge::ConsumeEditResults()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PropertyEditResult> results;
        results.reserve(edit_results_.size());
        while (!edit_results_.empty())
        {
            results.push_back(std::move(edit_results_.front()));
            outstanding_request_ids_.erase(results.back().request_id);
            edit_results_.pop_front();
            if (outstanding_edits_ > 0)
            {
                --outstanding_edits_;
            }
        }
        return results;
    }

    PropertyEditSubmission GameplayEditorBridge::SubmitPropertyEdit(
        PropertyEditCommand command)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (command.request_id == 0 || !command.actor.IsValid() ||
            !command.component.IsValid() || !command.expected_type.IsValid() ||
            !command.property.IsValid())
        {
            return {PropertyEditSubmissionStatus::InvalidArgument, command.request_id,
                    "Property edit command has an invalid identity"};
        }
        if (state_ != State::Running)
        {
            return {PropertyEditSubmissionStatus::Stopped, command.request_id,
                    "Gameplay editor bridge is not running"};
        }
        if (outstanding_request_ids_.find(command.request_id) != outstanding_request_ids_.end())
        {
            return {PropertyEditSubmissionStatus::InvalidArgument, command.request_id,
                    "Property edit request ID is already outstanding"};
        }
        if (outstanding_edits_ >= config_.max_outstanding_edits)
        {
            return {PropertyEditSubmissionStatus::QueueFull, command.request_id,
                    "Gameplay editor bridge edit capacity is exhausted"};
        }
        outstanding_request_ids_.insert(command.request_id);

        pending_edits_.push_back(std::move(command));
        ++outstanding_edits_;
        return {PropertyEditSubmissionStatus::Queued, pending_edits_.back().request_id, {}};
    }

    void GameplayEditorBridge::PumpEdits()
    {
        if (std::this_thread::get_id() != game_thread_id_)
        {
            return;
        }

        for (;;)
        {
            PropertyEditCommand command;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (state_ != State::Running || pending_edits_.empty())
                {
                    return;
                }
                command = std::move(pending_edits_.front());
                pending_edits_.pop_front();
                processing_edit_ = true;
            }

            PropertyEditResult result = ApplyEdit(command);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                processing_edit_ = false;
                edit_results_.push_back(std::move(result));
                processing_condition_.notify_all();
                if (state_ != State::Running)
                {
                    return;
                }
            }
        }
    }

    void GameplayEditorBridge::PublishSnapshot()
    {
        if (std::this_thread::get_id() != game_thread_id_)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::Running)
            {
                return;
            }
            building_snapshot_ = true;
        }

        auto snapshot = std::make_shared<GameplayEditorSnapshot>();
        snapshot->revision = next_snapshot_revision_++;

        std::vector<const Actor *> actors;
        actors.reserve(world_.actors_.size());
        for (const auto &[id, actor] : world_.actors_)
        {
            (void)id;
            if (actor != nullptr && actor->GetState() != ActorState::Destroyed)
            {
                actors.push_back(actor.get());
            }
        }
        std::sort(actors.begin(), actors.end(), [](const Actor *lhs, const Actor *rhs) {
            if (lhs->GetHandle().id != rhs->GetHandle().id)
            {
                return lhs->GetHandle().id < rhs->GetHandle().id;
            }
            return lhs->GetHandle().generation < rhs->GetHandle().generation;
        });

        std::size_t component_count = 0;
        std::size_t property_count = 0;
        std::size_t value_bytes = 0;
        for (const Actor *const actor : actors)
        {
            if (snapshot->actors.size() >= config_.max_actors)
            {
                snapshot->truncated = true;
                ++snapshot->omitted.actors;
                SaturatingAdd(snapshot->omitted.components, actor->components_.size());
                for (const std::unique_ptr<ActorComponent> &component : actor->components_)
                {
                    SaturatingAdd(snapshot->omitted.properties,
                                  CountReadableProperties(*component));
                }
                continue;
            }
            AppendActorSnapshot(*actor, *snapshot, component_count, property_count, value_bytes);
        }

        std::shared_ptr<const GameplayEditorSnapshot> immutable_snapshot = std::move(snapshot);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ == State::Running)
            {
                std::atomic_store_explicit(&latest_snapshot_, std::move(immutable_snapshot),
                                           std::memory_order_release);
            }
            building_snapshot_ = false;
            processing_condition_.notify_all();
        }
    }

    void GameplayEditorBridge::Shutdown() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_ == State::Stopped)
        {
            return;
        }
        state_ = State::Stopping;
        processing_condition_.wait(lock, [this] {
            return !processing_edit_ && !building_snapshot_;
        });

        while (!pending_edits_.empty())
        {
            PropertyEditResult result;
            result.request_id = pending_edits_.front().request_id;
            result.status = PropertyEditResultStatus::CancelledByShutdown;
            result.reflection_status = reflection::ReflectionResultStatus::ShutDown;
            result.diagnostic = "Property edit cancelled during bridge shutdown";
            edit_results_.push_back(std::move(result));
            pending_edits_.pop_front();
        }
        std::atomic_store_explicit(&latest_snapshot_,
                                   std::shared_ptr<const GameplayEditorSnapshot>{},
                                   std::memory_order_release);
        state_ = State::Stopped;
        processing_condition_.notify_all();
    }

    const GameplayEditorBridge::ResolvedBinding *GameplayEditorBridge::FindBinding(
        const ActorComponent &component) const
    {
        for (const ResolvedBinding &binding : bindings_)
        {
            if (binding.binding.matches(component))
            {
                return &binding;
            }
        }
        return nullptr;
    }

    const GameplayEditorBridge::ResolvedBinding *GameplayEditorBridge::FindBinding(
        reflection::ReflectionTypeId type) const
    {
        for (const ResolvedBinding &binding : bindings_)
        {
            if (binding.type == type)
            {
                return &binding;
            }
        }
        return nullptr;
    }

    PropertyEditResult GameplayEditorBridge::ApplyEdit(const PropertyEditCommand &command)
    {
        PropertyEditResult result;
        result.request_id = command.request_id;

        Actor *const actor = world_.FindActor(command.actor);
        if (actor == nullptr)
        {
            result.status = PropertyEditResultStatus::StaleActor;
            result.reflection_status = reflection::ReflectionResultStatus::InvalidObject;
            result.diagnostic = "Actor handle is stale or unavailable";
            return result;
        }

        ActorComponent *component = nullptr;
        for (const std::unique_ptr<ActorComponent> &candidate : actor->components_)
        {
            if (candidate->GetInstanceId() == command.component)
            {
                component = candidate.get();
                break;
            }
        }
        if (component == nullptr)
        {
            result.status = PropertyEditResultStatus::StaleComponent;
            result.reflection_status = reflection::ReflectionResultStatus::InvalidObject;
            result.diagnostic = "Component instance is stale or unavailable";
            return result;
        }

        const ResolvedBinding *const binding = FindBinding(*component);
        if (binding == nullptr || binding->type != command.expected_type)
        {
            result.status = PropertyEditResultStatus::ReflectedTypeMismatch;
            result.reflection_status = reflection::ReflectionResultStatus::WrongObjectType;
            result.diagnostic = "Live component type does not match the edit expectation";
            return result;
        }

        const reflection::ReflectionPropertyDescriptor *const property =
            catalog_.FindProperty(binding->type, command.property);
        if (property == nullptr)
        {
            result.status = PropertyEditResultStatus::UnknownProperty;
            result.reflection_status = reflection::ReflectionResultStatus::UnknownProperty;
            result.diagnostic = "Property is not present on the reflected component type";
            return result;
        }
        if (!reflection::HasFlag(property->flags, reflection::ReflectionPropertyFlags::Writable))
        {
            result.status = PropertyEditResultStatus::NotWritable;
            result.reflection_status = reflection::ReflectionResultStatus::ReadOnly;
            result.diagnostic = "Property is not writable";
            return result;
        }

        const reflection::ReflectionObjectRef object =
            binding->binding.make_mutable_object(binding->type, component);
        const reflection::ReflectionResult write =
            access_.Write(object, command.property, command.value);
        if (!write)
        {
            result.status = PropertyEditResultStatus::ValueRejected;
            result.reflection_status = write.status;
            result.diagnostic = write.diagnostic;
            return result;
        }

        const reflection::ReflectionReadResult read = access_.Read(object, command.property);
        if (!read)
        {
            result.status = PropertyEditResultStatus::ReadbackFailed;
            result.reflection_status = read.status;
            result.diagnostic = read.diagnostic;
            return result;
        }

        result.status = PropertyEditResultStatus::Applied;
        result.reflection_status = reflection::ReflectionResultStatus::Success;
        result.value = read.value;
        return result;
    }

    std::size_t GameplayEditorBridge::CountReadableProperties(
        const ActorComponent &component) const
    {
        const ResolvedBinding *const binding = FindBinding(component);
        if (binding == nullptr)
        {
            return 0;
        }
        const reflection::ReflectionTypeDescriptor *const type =
            catalog_.FindType(binding->type);
        if (type == nullptr)
        {
            return 0;
        }
        return static_cast<std::size_t>(std::count_if(
            type->properties.begin(), type->properties.end(), [](const auto &property) {
                return reflection::HasFlag(property.flags,
                                            reflection::ReflectionPropertyFlags::Readable) &&
                       reflection::HasFlag(property.flags,
                                            reflection::ReflectionPropertyFlags::EditorVisible);
            }));
    }

    void GameplayEditorBridge::AppendActorSnapshot(const Actor &actor,
                                                   GameplayEditorSnapshot &snapshot,
                                                   std::size_t &component_count,
                                                   std::size_t &property_count,
                                                   std::size_t &value_bytes)
    {
        ActorEditorSnapshot actor_snapshot;
        actor_snapshot.actor = actor.GetHandle();
        actor_snapshot.state = actor.GetState();
        CopyStringWithinBudget(MakeActorDisplayName(actor.GetHandle()),
                               actor_snapshot.display_name, snapshot, value_bytes);
        if (actor.root_component_ != nullptr)
        {
            actor_snapshot.root_component = actor.root_component_->GetInstanceId();
        }

        for (const std::unique_ptr<ActorComponent> &component : actor.components_)
        {
            const std::size_t readable_properties = CountReadableProperties(*component);
            if (component_count >= config_.max_components)
            {
                snapshot.truncated = true;
                ++snapshot.omitted.components;
                SaturatingAdd(snapshot.omitted.properties, readable_properties);
                continue;
            }
            ++component_count;

            ComponentEditorSnapshot component_snapshot;
            component_snapshot.component = component->GetInstanceId();
            component_snapshot.is_root = actor.root_component_ == component.get();
            const ResolvedBinding *const binding = FindBinding(*component);
            if (binding == nullptr)
            {
                CopyStringWithinBudget(
                    "Component type is not available through Gameplay reflection",
                    component_snapshot.diagnostic, snapshot, value_bytes);
                actor_snapshot.components.push_back(std::move(component_snapshot));
                continue;
            }

            component_snapshot.type = binding->type;
            const reflection::ReflectionTypeDescriptor *const type =
                catalog_.FindType(binding->type);
            const reflection::ReflectionObjectRef object =
                binding->binding.make_const_object(binding->type, *component);
            if (type == nullptr || !object.IsValid())
            {
                CopyStringWithinBudget("Reflected component object is invalid",
                                       component_snapshot.diagnostic, snapshot, value_bytes);
                actor_snapshot.components.push_back(std::move(component_snapshot));
                continue;
            }

            for (const reflection::ReflectionPropertyDescriptor &property : type->properties)
            {
                if (!reflection::HasFlag(property.flags,
                                         reflection::ReflectionPropertyFlags::Readable) ||
                    !reflection::HasFlag(property.flags,
                                         reflection::ReflectionPropertyFlags::EditorVisible))
                {
                    continue;
                }
                if (property_count >= config_.max_properties)
                {
                    snapshot.truncated = true;
                    ++snapshot.omitted.properties;
                    continue;
                }

                const reflection::ReflectionReadResult read = access_.Read(object, property.id);
                const std::size_t copied_bytes = read ? EstimateValueBytes(read.value) : 0;
                if (read && !AddWithinBudget(value_bytes, copied_bytes, config_.max_value_bytes))
                {
                    snapshot.truncated = true;
                    SaturatingAdd(snapshot.omitted.properties, 1);
                    SaturatingAdd(snapshot.omitted.value_bytes, copied_bytes);
                    SaturatingAdd(snapshot.omitted.value_bytes, read.diagnostic.size());
                    continue;
                }

                PropertyValueSnapshot property_snapshot;
                property_snapshot.property = property.id;
                property_snapshot.status = read.status;
                property_snapshot.value = read.value;
                CopyStringWithinBudget(read.diagnostic, property_snapshot.diagnostic, snapshot,
                                       value_bytes);
                component_snapshot.properties.push_back(std::move(property_snapshot));
                ++property_count;
                value_bytes += copied_bytes;
            }
            actor_snapshot.components.push_back(std::move(component_snapshot));
        }
        snapshot.actors.push_back(std::move(actor_snapshot));
    }

    void GameplayEditorBridge::CopyStringWithinBudget(std::string_view source,
                                                      std::string &destination,
                                                      GameplayEditorSnapshot &snapshot,
                                                      std::size_t &value_bytes) const
    {
        if (source.empty())
        {
            return;
        }
        if (AddWithinBudget(value_bytes, source.size(), config_.max_value_bytes))
        {
            destination.assign(source);
            value_bytes += source.size();
            return;
        }
        snapshot.truncated = true;
        SaturatingAdd(snapshot.omitted.value_bytes, source.size());
    }

    std::size_t GameplayEditorBridge::EstimateValueBytes(
        const reflection::ReflectionValue &value) noexcept
    {
        if (const std::string *const string_value = value.TryGet<std::string>())
        {
            return string_value->size();
        }
        if (value.TryGet<bool>() != nullptr)
        {
            return sizeof(bool);
        }
        if (value.TryGet<int64_t>() != nullptr || value.TryGet<uint64_t>() != nullptr ||
            value.TryGet<double>() != nullptr)
        {
            return sizeof(double);
        }
        return 0;
    }

    std::string GameplayEditorBridge::MakeActorDisplayName(ActorHandle handle)
    {
        return "Actor " + std::to_string(handle.id) + ":" +
               std::to_string(handle.generation);
    }
}
