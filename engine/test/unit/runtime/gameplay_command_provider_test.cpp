#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "command/command_registry.h"
#include "gameplay/actor/actor.h"
#include "gameplay/component/camera_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/controller/player_controller.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "gameplay_command/gameplay_command_provider.h"

namespace kpengine::runtime
{
    namespace
    {
        const uint64_t *GetUnsigned(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr
                                          : std::get_if<uint64_t>(&iterator->second);
        }

        const std::string *GetString(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr
                                          : std::get_if<std::string>(&iterator->second);
        }

        const double *GetFloat(const command::CommandData &data, const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr : std::get_if<double>(&iterator->second);
        }

        command::CommandResult ExecuteAndPump(command::CommandRegistry &registry,
                                              command::CommandCall call,
                                              const command::CommandCapability capabilities =
                                                  command::CommandCapability::None)
        {
            const command::CommandResult pending = registry.Execute(
                call, {command::CommandOrigin::Agent, command::CommandThread::Immediate,
                       capabilities});
            if (pending.status != command::CommandStatus::Pending)
            {
                return pending;
            }
            if (registry.PumpGameThread() != 1U)
            {
                return {command::CommandStatus::Failed, "game lane did not run", 0, {}};
            }
            const std::optional<command::CommandResult> completion =
                registry.TakeCompletion(pending.request_id);
            return completion.has_value()
                       ? *completion
                       : command::CommandResult{command::CommandStatus::Failed,
                                                "no completion recorded", 0, {}};
        }

        gameplay::ActorHandle CreateNamedRootActor(gameplay::GameplayWorld &world,
                                                   std::string name,
                                                   const Transform3f &transform = {})
        {
            const gameplay::ActorHandle handle = world.CreateActor();
            gameplay::Actor *const actor = world.FindActor(handle);
            if (actor == nullptr)
            {
                return {};
            }
            actor->SetName(std::move(name));
            gameplay::SceneComponent *const root =
                actor->AddComponent<gameplay::SceneComponent>();
            if (root == nullptr || !actor->SetRootComponent(root))
            {
                return {};
            }
            root->SetLocalTransform(transform);
            return handle;
        }
    }

    TEST(GameplayCommandProviderTest, RegistersAgentCommandsWithDiscoverableSchemas)
    {
        gameplay::GameplayWorld world{};
        command::CommandRegistry registry;
        const auto registration = RegisterGameplayCommands(
            registry, [&world]() -> gameplay::GameplayWorld * { return &world; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const command::CommandResult commands = registry.Execute(
            {"commands.list", {}},
            {command::CommandOrigin::Agent, command::CommandThread::Immediate});
        ASSERT_EQ(commands.status, command::CommandStatus::Success);
        EXPECT_NE(commands.message.find("actor.list"), std::string::npos);
        EXPECT_NE(commands.message.find("actor.query"), std::string::npos);
        EXPECT_NE(commands.message.find("actor.control"), std::string::npos);

        const command::CommandResult help = registry.Execute(
            {"help", {{"name", std::string{"actor.list"}}}},
            {command::CommandOrigin::Agent, command::CommandThread::Immediate});
        ASSERT_EQ(help.status, command::CommandStatus::Success) << help.message;
        EXPECT_NE(help.message.find("name_contains"), std::string::npos);
        EXPECT_NE(help.message.find("offset"), std::string::npos);
        EXPECT_NE(help.message.find("limit"), std::string::npos);
    }

    TEST(GameplayCommandProviderTest, ListsAndFiltersByNameBeforePaging)
    {
        gameplay::GameplayWorld world{};
        const auto first = CreateNamedRootActor(world, "Bunny");
        const auto second = CreateNamedRootActor(world, "bunny_small");
        const auto third = CreateNamedRootActor(world, "Fox");
        ASSERT_TRUE(first.IsValid());
        ASSERT_TRUE(second.IsValid());
        ASSERT_TRUE(third.IsValid());

        command::CommandRegistry registry;
        const auto registration = RegisterGameplayCommands(
            registry, [&world]() -> gameplay::GameplayWorld * { return &world; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        command::CommandResult result = ExecuteAndPump(
            registry,
            {"actor.list", {{"name_contains", std::string{"BUNNY"}},
                             {"offset", uint64_t{1}}, {"limit", uint64_t{1}}}});
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetUnsigned(result.data, "total_count"), nullptr);
        ASSERT_NE(GetUnsigned(result.data, "count"), nullptr);
        ASSERT_NE(GetString(result.data, "actors.0.name"), nullptr);
        ASSERT_NE(GetUnsigned(result.data, "actors.0.id"), nullptr);
        ASSERT_NE(GetUnsigned(result.data, "actors.0.generation"), nullptr);
        EXPECT_EQ(*GetUnsigned(result.data, "total_count"), 2U);
        EXPECT_EQ(*GetUnsigned(result.data, "count"), 1U);
        EXPECT_EQ(*GetString(result.data, "actors.0.name"), "bunny_small");
        EXPECT_EQ(*GetUnsigned(result.data, "actors.0.id"), second.id);
        EXPECT_EQ(*GetUnsigned(result.data, "actors.0.generation"), second.generation);

        result = ExecuteAndPump(registry, {"actor.list", {}});
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetUnsigned(result.data, "total_count"), nullptr);
        ASSERT_NE(GetString(result.data, "actors.2.name"), nullptr);
        EXPECT_EQ(*GetUnsigned(result.data, "total_count"), 3U);
        EXPECT_EQ(*GetString(result.data, "actors.2.name"), "Fox");
    }

    TEST(GameplayCommandProviderTest, QueriesAndControlsActorThroughGameplay)
    {
        gameplay::GameplayWorld world{};
        const Transform3f initial{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f},
                                  {2.0f, 3.0f, 4.0f}};
        const auto handle = CreateNamedRootActor(world, "camera rig", initial);
        ASSERT_TRUE(handle.IsValid());

        command::CommandRegistry registry;
        const auto registration = RegisterGameplayCommands(
            registry, [&world]() -> gameplay::GameplayWorld * { return &world; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        command::CommandResult result = ExecuteAndPump(
            registry, {"actor.query", {{"id", static_cast<uint64_t>(handle.id)},
                                       {"generation", static_cast<uint64_t>(handle.generation)}}});
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetString(result.data, "name"), nullptr);
        ASSERT_NE(GetFloat(result.data, "root.local.position.x"), nullptr);
        EXPECT_EQ(*GetString(result.data, "name"), "camera rig");
        EXPECT_DOUBLE_EQ(*GetFloat(result.data, "root.local.position.x"), 1.0);

        const command::CommandResult unauthorized = registry.Execute(
            {"actor.control", {{"id", static_cast<uint64_t>(handle.id)},
                                {"generation", static_cast<uint64_t>(handle.generation)},
                                {"local_position_x", 10.0}, {"local_position_y", 20.0},
                                {"local_position_z", 30.0}, {"local_pitch", 11.0},
                                {"local_yaw", 22.0}, {"local_roll", 33.0}}},
            {command::CommandOrigin::Agent, command::CommandThread::Immediate});
        EXPECT_EQ(unauthorized.status, command::CommandStatus::Denied);
        EXPECT_DOUBLE_EQ(world.FindActor(handle)->GetRootComponent()->GetLocalLocation().x_, 1.0f);

        const command::CommandResult invalid = ExecuteAndPump(
            registry,
            {"actor.control", {{"id", static_cast<uint64_t>(handle.id)},
                               {"generation", static_cast<uint64_t>(handle.generation)},
                               {"local_position_x", std::numeric_limits<double>::max()},
                               {"local_position_y", 20.0}, {"local_position_z", 30.0},
                               {"local_pitch", 11.0}, {"local_yaw", 22.0},
                               {"local_roll", 33.0}}},
            command::CommandCapability::Mutating);
        EXPECT_EQ(invalid.status, command::CommandStatus::InvalidArguments);
        EXPECT_FLOAT_EQ(world.FindActor(handle)->GetRootComponent()->GetLocalLocation().x_, 1.0f);

        result = ExecuteAndPump(
            registry,
            {"actor.control", {{"id", static_cast<uint64_t>(handle.id)},
                               {"generation", static_cast<uint64_t>(handle.generation)},
                               {"local_position_x", 10.0}, {"local_position_y", 20.0},
                               {"local_position_z", 30.0}, {"local_pitch", 11.0},
                               {"local_yaw", 22.0}, {"local_roll", 33.0}}},
            command::CommandCapability::Mutating);
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetFloat(result.data, "applied.position.x"), nullptr);
        ASSERT_NE(GetFloat(result.data, "applied.scale.x"), nullptr);
        EXPECT_DOUBLE_EQ(*GetFloat(result.data, "applied.position.x"), 10.0);
        EXPECT_DOUBLE_EQ(*GetFloat(result.data, "applied.scale.x"), 2.0);

        result = ExecuteAndPump(
            registry, {"actor.query", {{"id", static_cast<uint64_t>(handle.id)},
                                       {"generation", static_cast<uint64_t>(handle.generation)}}});
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        ASSERT_NE(GetFloat(result.data, "root.local.position.z"), nullptr);
        EXPECT_DOUBLE_EQ(*GetFloat(result.data, "root.local.position.z"), 30.0);

        ASSERT_TRUE(world.DestroyActor(handle));
        result = ExecuteAndPump(
            registry, {"actor.query", {{"id", static_cast<uint64_t>(handle.id)},
                                       {"generation", static_cast<uint64_t>(handle.generation)}}});
        EXPECT_EQ(result.status, command::CommandStatus::NotFound);
    }

    TEST(GameplayCommandProviderTest, ControlCommandKeepsPossessedCameraRotationInSync)
    {
        gameplay::GameplayWorld world{};
        gameplay::CameraActorDesc description;
        description.transform.rotator_ = {0.0f, -90.0f, 0.0f};
        const gameplay::ActorHandle handle = gameplay::CreateCameraActor(world, description);
        ASSERT_TRUE(handle.IsValid());
        gameplay::PlayerController *const controller = world.CreateLocalPlayerController(nullptr);
        ASSERT_NE(controller, nullptr);
        ASSERT_TRUE(controller->Possess(handle));

        command::CommandRegistry registry;
        const auto registration = RegisterGameplayCommands(
            registry, [&world]() -> gameplay::GameplayWorld * { return &world; });
        ASSERT_TRUE(registration.IsSuccess()) << registration.diagnostic;

        const command::CommandResult result = ExecuteAndPump(
            registry,
            {"actor.control", {{"id", static_cast<uint64_t>(handle.id)},
                               {"generation", static_cast<uint64_t>(handle.generation)},
                               {"local_position_x", 12.0}, {"local_position_y", 24.0},
                               {"local_position_z", 36.0}, {"local_pitch", 20.0},
                               {"local_yaw", 145.0}, {"local_roll", 0.0}}},
            command::CommandCapability::Mutating);
        ASSERT_EQ(result.status, command::CommandStatus::Success) << result.message;
        EXPECT_FLOAT_EQ(controller->GetControlRotation().pitch_, 20.0f);
        EXPECT_FLOAT_EQ(controller->GetControlRotation().yaw_, 145.0f);

        world.Tick(0.0f);
        const auto *const camera = world.FindActor(handle)->FindComponent<
            gameplay::CameraComponent>();
        ASSERT_NE(camera, nullptr);
        EXPECT_FLOAT_EQ(camera->GetLocalTransform().rotator_.yaw_, 145.0f);
    }
}
