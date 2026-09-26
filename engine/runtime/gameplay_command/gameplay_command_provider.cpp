#include "gameplay_command/gameplay_command_provider.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "gameplay/world/gameplay_world.h"

namespace kpengine::runtime
{
    namespace
    {
        constexpr uint64_t kDefaultActorPageSize = 32;
        constexpr uint64_t kMaximumActorPageSize = 64;

        bool ResolveUnsigned(const command::CommandArguments &arguments,
                             const char *name, uint64_t &out)
        {
            const auto iterator = arguments.find(name);
            if (iterator == arguments.end())
            {
                return false;
            }
            const auto *const value = std::get_if<uint64_t>(&iterator->second);
            if (value == nullptr)
            {
                return false;
            }
            out = *value;
            return true;
        }

        bool ResolveString(const command::CommandArguments &arguments,
                           const char *name, std::string &out)
        {
            const auto iterator = arguments.find(name);
            if (iterator == arguments.end())
            {
                return false;
            }
            const auto *const value = std::get_if<std::string>(&iterator->second);
            if (value == nullptr)
            {
                return false;
            }
            out = *value;
            return true;
        }

        bool ResolveFiniteFloat(const command::CommandArguments &arguments,
                                const char *name, float &out)
        {
            const auto iterator = arguments.find(name);
            if (iterator == arguments.end())
            {
                return false;
            }
            const auto *const value = std::get_if<double>(&iterator->second);
            if (value == nullptr || !std::isfinite(*value) ||
                *value < -std::numeric_limits<float>::max() ||
                *value > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = static_cast<float>(*value);
            return std::isfinite(out);
        }

        bool ResolveActorHandle(const command::CommandArguments &arguments,
                                gameplay::ActorHandle &out)
        {
            uint64_t id = 0;
            uint64_t generation = 0;
            if (!ResolveUnsigned(arguments, "id", id) ||
                !ResolveUnsigned(arguments, "generation", generation) ||
                id >= std::numeric_limits<uint32_t>::max() ||
                generation > std::numeric_limits<uint16_t>::max())
            {
                return false;
            }
            out = gameplay::ActorHandle{static_cast<uint32_t>(id),
                                        static_cast<uint16_t>(generation)};
            return out.IsValid();
        }

        const char *ActorStateName(const gameplay::ActorState state) noexcept
        {
            switch (state)
            {
            case gameplay::ActorState::Constructed: return "constructed";
            case gameplay::ActorState::Initialized: return "initialized";
            case gameplay::ActorState::Active: return "active";
            case gameplay::ActorState::Inactive: return "inactive";
            case gameplay::ActorState::Destroyed: return "destroyed";
            }
            return "unknown";
        }

        command::CommandArgumentDesc UnsignedArgument(
            std::string name, const bool required,
            command::CommandValue default_value = {})
        {
            return {std::move(name), command::CommandValueType::UnsignedInteger,
                    required, std::move(default_value), {}};
        }

        command::CommandArgumentDesc FloatArgument(std::string name)
        {
            return {std::move(name), command::CommandValueType::Float, true, {}, {}};
        }

        command::CommandArgumentDesc StringArgument(std::string name, std::string default_value)
        {
            return {std::move(name), command::CommandValueType::String, false,
                    std::move(default_value), {}};
        }

        void AddTransformData(command::CommandData &data, const std::string &prefix,
                              const Transform3f &transform)
        {
            data.emplace(prefix + ".position.x", static_cast<double>(transform.position_.x_));
            data.emplace(prefix + ".position.y", static_cast<double>(transform.position_.y_));
            data.emplace(prefix + ".position.z", static_cast<double>(transform.position_.z_));
            data.emplace(prefix + ".rotation.pitch", static_cast<double>(transform.rotator_.pitch_));
            data.emplace(prefix + ".rotation.yaw", static_cast<double>(transform.rotator_.yaw_));
            data.emplace(prefix + ".rotation.roll", static_cast<double>(transform.rotator_.roll_));
            data.emplace(prefix + ".scale.x", static_cast<double>(transform.scale_.x_));
            data.emplace(prefix + ".scale.y", static_cast<double>(transform.scale_.y_));
            data.emplace(prefix + ".scale.z", static_cast<double>(transform.scale_.z_));
        }

        std::vector<command::CommandArgumentDesc> MakeHandleSchema()
        {
            return {UnsignedArgument("id", true), UnsignedArgument("generation", true)};
        }

        command::CommandResult MakeUnavailableResult(const uint64_t request_id)
        {
            return {command::CommandStatus::Failed, "GameplayWorld is unavailable",
                    request_id, {}};
        }
    }

    GameplayCommandRegistrationResult RegisterGameplayCommands(
        command::CommandRegistry &registry,
        std::function<gameplay::GameplayWorld *()> world_resolver)
    {
        if (!world_resolver)
        {
            return {{}, command::CommandRegistrationStatus::InvalidDescriptor,
                    "Gameplay commands require a GameplayWorld resolver"};
        }

        command::CommandDesc list_descriptor{
            "actor.list",
            "RuntimeGameplay",
            "List live Actors by generational handle",
            command::CommandCategory::Gameplay,
            command::CommandFlags::AgentAllowed,
            {{UnsignedArgument("offset", false, uint64_t{0}),
              UnsignedArgument("limit", false, kDefaultActorPageSize),
              StringArgument("name_contains", "")}},
            [resolver = world_resolver](const command::CommandCall &call,
                                        const command::CommandContext &context)
            {
                uint64_t offset = 0;
                uint64_t limit = 0;
                std::string name_contains;
                if (!ResolveUnsigned(call.arguments, "offset", offset) ||
                    !ResolveUnsigned(call.arguments, "limit", limit) || limit == 0 ||
                    limit > kMaximumActorPageSize ||
                    offset > std::numeric_limits<std::size_t>::max() ||
                    !ResolveString(call.arguments, "name_contains", name_contains))
                {
                    return command::CommandResult{
                        command::CommandStatus::InvalidArguments,
                        "actor.list requires offset >= 0 and limit from 1 to 64",
                        context.request_id, {}};
                }

                gameplay::GameplayWorld *const world = resolver();
                if (world == nullptr)
                {
                    return MakeUnavailableResult(context.request_id);
                }

                const gameplay::ActorListPage page = world->ListActors(
                    static_cast<std::size_t>(offset), static_cast<std::size_t>(limit),
                    name_contains);
                const uint64_t page_count = static_cast<uint64_t>(page.actors.size());
                const uint64_t next_offset =
                    page_count > std::numeric_limits<uint64_t>::max() - offset
                        ? std::numeric_limits<uint64_t>::max()
                        : offset + page_count;
                command::CommandData data{
                    {"count", static_cast<uint64_t>(page.actors.size())},
                    {"total_count", static_cast<uint64_t>(page.total_count)},
                    {"has_more", page.has_more},
                    {"next_offset", next_offset}};
                for (std::size_t index = 0; index < page.actors.size(); ++index)
                {
                    const gameplay::ActorSummary &actor = page.actors[index];
                    const std::string prefix = "actors." + std::to_string(index) + ".";
                    data.emplace(prefix + "id", static_cast<uint64_t>(actor.handle.id));
                    data.emplace(prefix + "generation",
                                 static_cast<uint64_t>(actor.handle.generation));
                    data.emplace(prefix + "name", actor.name);
                    data.emplace(prefix + "state", std::string{ActorStateName(actor.state)});
                    data.emplace(prefix + "has_root_component", actor.has_root_component);
                }

                return command::CommandResult{
                    command::CommandStatus::Success, "Actor page listed",
                    context.request_id, std::move(data)};
            },
            command::CommandThread::Game};

        command::CommandDesc query_descriptor{
            "actor.query",
            "RuntimeGameplay",
            "Query one Actor and its root transform by handle",
            command::CommandCategory::Gameplay,
            command::CommandFlags::AgentAllowed,
            {MakeHandleSchema()},
            [resolver = world_resolver](const command::CommandCall &call,
                                        const command::CommandContext &context)
            {
                gameplay::ActorHandle handle;
                if (!ResolveActorHandle(call.arguments, handle))
                {
                    return command::CommandResult{
                        command::CommandStatus::InvalidArguments,
                        "actor.query requires a valid unsigned id and generation",
                        context.request_id, {}};
                }

                gameplay::GameplayWorld *const world = resolver();
                if (world == nullptr)
                {
                    return MakeUnavailableResult(context.request_id);
                }
                const std::optional<gameplay::ActorQuery> query = world->QueryActor(handle);
                if (!query.has_value())
                {
                    return command::CommandResult{
                        command::CommandStatus::NotFound,
                        "Actor handle is stale or unavailable", context.request_id, {}};
                }

                command::CommandData data{
                    {"id", static_cast<uint64_t>(query->actor.handle.id)},
                    {"generation", static_cast<uint64_t>(query->actor.handle.generation)},
                    {"name", query->actor.name},
                    {"state", std::string{ActorStateName(query->actor.state)}},
                    {"has_root_component", query->actor.has_root_component}};
                if (query->root_local_transform.has_value())
                {
                    AddTransformData(data, "root.local", *query->root_local_transform);
                }
                if (query->root_world_transform.has_value())
                {
                    AddTransformData(data, "root.world", *query->root_world_transform);
                }
                return command::CommandResult{
                    command::CommandStatus::Success, "Actor queried",
                    context.request_id, std::move(data)};
            },
            command::CommandThread::Game};

        std::vector<command::CommandArgumentDesc> control_arguments = MakeHandleSchema();
        control_arguments.push_back(FloatArgument("local_position_x"));
        control_arguments.push_back(FloatArgument("local_position_y"));
        control_arguments.push_back(FloatArgument("local_position_z"));
        control_arguments.push_back(FloatArgument("local_pitch"));
        control_arguments.push_back(FloatArgument("local_yaw"));
        control_arguments.push_back(FloatArgument("local_roll"));
        command::CommandDesc control_descriptor{
            "actor.control",
            "RuntimeGameplay",
            "Set an Actor root component's local position and rotation",
            command::CommandCategory::Gameplay,
            command::CommandFlags::AgentAllowed | command::CommandFlags::MutatesState,
            {std::move(control_arguments)},
            [resolver = world_resolver](const command::CommandCall &call,
                                        const command::CommandContext &context)
            {
                gameplay::ActorHandle handle;
                Vector3f position;
                Rotatorf rotation;
                if (!ResolveActorHandle(call.arguments, handle) ||
                    !ResolveFiniteFloat(call.arguments, "local_position_x", position.x_) ||
                    !ResolveFiniteFloat(call.arguments, "local_position_y", position.y_) ||
                    !ResolveFiniteFloat(call.arguments, "local_position_z", position.z_) ||
                    !ResolveFiniteFloat(call.arguments, "local_pitch", rotation.pitch_) ||
                    !ResolveFiniteFloat(call.arguments, "local_yaw", rotation.yaw_) ||
                    !ResolveFiniteFloat(call.arguments, "local_roll", rotation.roll_))
                {
                    return command::CommandResult{
                        command::CommandStatus::InvalidArguments,
                        "actor.control requires a valid handle and finite float transform values",
                        context.request_id, {}};
                }

                gameplay::GameplayWorld *const world = resolver();
                if (world == nullptr)
                {
                    return MakeUnavailableResult(context.request_id);
                }
                const gameplay::ActorTransformControlResult applied =
                    world->SetActorRootTransform(handle, position, rotation);
                if (!applied.IsSuccess())
                {
                    const command::CommandStatus status =
                        applied.status == gameplay::ActorTransformControlStatus::ActorUnavailable
                            ? command::CommandStatus::NotFound
                            : command::CommandStatus::Failed;
                    const char *message = "Actor transform control failed";
                    if (applied.status ==
                        gameplay::ActorTransformControlStatus::MissingRootComponent)
                    {
                        message = "Actor has no root SceneComponent";
                    }
                    else if (applied.status ==
                             gameplay::ActorTransformControlStatus::InvalidTransform)
                    {
                        message = "Actor transform contains a non-finite value";
                    }
                    return command::CommandResult{status, message, context.request_id, {}};
                }

                command::CommandData data{
                    {"id", static_cast<uint64_t>(handle.id)},
                    {"generation", static_cast<uint64_t>(handle.generation)}};
                AddTransformData(data, "applied", applied.applied_transform);
                return command::CommandResult{
                    command::CommandStatus::Success, "Actor transform applied",
                    context.request_id, std::move(data)};
            },
            command::CommandThread::Game};

        GameplayCommandRegistrationResult result;
        command::CommandRegistrationResult list_registration =
            registry.Register(std::move(list_descriptor));
        if (!list_registration.IsSuccess())
        {
            return {{}, list_registration.status, std::move(list_registration.diagnostic)};
        }
        result.registrations[0] = std::move(list_registration.registration);

        command::CommandRegistrationResult query_registration =
            registry.Register(std::move(query_descriptor));
        if (!query_registration.IsSuccess())
        {
            return {{}, query_registration.status, std::move(query_registration.diagnostic)};
        }
        result.registrations[1] = std::move(query_registration.registration);

        command::CommandRegistrationResult control_registration =
            registry.Register(std::move(control_descriptor));
        if (!control_registration.IsSuccess())
        {
            return {{}, control_registration.status,
                    std::move(control_registration.diagnostic)};
        }
        result.registrations[2] = std::move(control_registration.registration);
        result.status = command::CommandRegistrationStatus::Registered;
        return result;
    }
}
