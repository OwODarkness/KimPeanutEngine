#include <atomic>
#include <algorithm>
#include <string_view>
#include <thread>
#include <unordered_set>

#include <gtest/gtest.h>

#include "gameplay/actor/actor.h"
#include "gameplay/component/camera_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/editor_bridge/gameplay_editor_bridge.h"
#include "gameplay/reflection/gameplay_reflection.h"
#include "gameplay/world/gameplay_world.h"
#include "reflection/entt/entt_reflection_registry.h"
#include "reflection/i_reflection_catalog.h"
#include "reflection/reflection_system.h"

namespace
{
    class UnreflectedComponent final : public kpengine::gameplay::ActorComponent
    {
    };

    class ReadOnlyCatalogView final : public kpengine::reflection::IReflectionCatalog
    {
    public:
        explicit ReadOnlyCatalogView(const kpengine::reflection::IReflectionCatalog &catalog)
            : catalog_(catalog)
        {
        }

        const kpengine::reflection::ReflectionTypeDescriptor *FindType(
            kpengine::reflection::ReflectionTypeId type) const noexcept override
        {
            return Prepare(catalog_.FindType(type));
        }

        const kpengine::reflection::ReflectionTypeDescriptor *FindType(
            std::string_view name) const noexcept override
        {
            return Prepare(catalog_.FindType(name));
        }

        const kpengine::reflection::ReflectionPropertyDescriptor *FindProperty(
            kpengine::reflection::ReflectionTypeId type,
            kpengine::reflection::ReflectionPropertyId property) const noexcept override
        {
            const auto *const descriptor = FindType(type);
            if (descriptor == nullptr)
            {
                return nullptr;
            }
            for (const auto &candidate : descriptor->properties)
            {
                if (candidate.id == property)
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        std::vector<kpengine::reflection::ReflectionTypeDescriptor> EnumerateTypes()
            const override
        {
            auto types = catalog_.EnumerateTypes();
            for (auto &type : types)
            {
                MakeReadOnly(type);
            }
            return types;
        }

    private:
        static void MakeReadOnly(kpengine::reflection::ReflectionTypeDescriptor &type)
        {
            if (type.name != "kpengine.gameplay.SceneComponent")
            {
                return;
            }
            for (auto &property : type.properties)
            {
                if (property.name == "transform.location.x")
                {
                    property.flags = static_cast<kpengine::reflection::ReflectionPropertyFlags>(
                        static_cast<uint8_t>(property.flags) &
                        ~static_cast<uint8_t>(kpengine::reflection::ReflectionPropertyFlags::Writable));
                }
            }
        }

        const kpengine::reflection::ReflectionTypeDescriptor *Prepare(
            const kpengine::reflection::ReflectionTypeDescriptor *type) const noexcept
        {
            if (type == nullptr || type->name != "kpengine.gameplay.SceneComponent")
            {
                return type;
            }
            scene_type_ = *type;
            MakeReadOnly(scene_type_);
            return &scene_type_;
        }

        const kpengine::reflection::IReflectionCatalog &catalog_;
        mutable kpengine::reflection::ReflectionTypeDescriptor scene_type_;
    };

    const kpengine::reflection::ReflectionPropertyDescriptor *FindProperty(
        const kpengine::reflection::IReflectionCatalog &catalog,
        const char *type_name,
        const char *property_name)
    {
        const auto *const type = catalog.FindType(type_name);
        if (type == nullptr)
        {
            return nullptr;
        }
        for (const auto &property : type->properties)
        {
            if (property.name == property_name)
            {
                return &property;
            }
        }
        return nullptr;
    }

    struct ReflectionFixture
    {
        kpengine::reflection::ReflectionSystem system;
        kpengine::gameplay::GameplayWorld world;

        ReflectionFixture()
        {
            (void)system.Initialize({kpengine::gameplay::RegisterGameplayReflection});
        }

        ~ReflectionFixture()
        {
            system.Shutdown();
        }
    };
}

TEST(GameplayEditorBridgeTest, PublishesBoundedCopiedSnapshotsWithStableComponentIds)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());

    const kpengine::gameplay::ActorHandle actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const first = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const second = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const auto first_id = first->GetInstanceId();
    const auto second_id = second->GetInstanceId();
    ASSERT_TRUE(actor->SetRootComponent(first));

    bridge.PublishSnapshot();
    const auto snapshot = bridge.GetLatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->actors.size(), 1U);
    ASSERT_EQ(snapshot->actors.front().components.size(), 2U);
    ASSERT_TRUE(snapshot->actors.front().root_component.has_value());
    EXPECT_EQ(*snapshot->actors.front().root_component, first_id);
    EXPECT_NE(snapshot->actors.front().components[0].component,
              snapshot->actors.front().components[1].component);
    EXPECT_EQ(snapshot->actors.front().components[0].type,
              fixture.system.GetCatalog()->FindType("kpengine.gameplay.SceneComponent")->id);
    EXPECT_FALSE(snapshot->actors.front().components[0].properties.empty());

    fixture.world.DestroyActor(actor_handle);
    fixture.world.ReclaimDestroyedActors();
    EXPECT_EQ(snapshot->actors.front().actor, actor_handle);
    EXPECT_EQ(snapshot->actors.front().components[0].component, first_id);
    EXPECT_EQ(snapshot->actors.front().components[1].component, second_id);
}

TEST(GameplayEditorBridgeTest, ReflectionBindingsMatchEveryCatalogTypeExactlyOnce)
{
    ReflectionFixture fixture;
    const auto bindings = kpengine::gameplay::GetGameplayReflectionBindings();
    const auto types = fixture.system.GetCatalog()->EnumerateTypes();
    std::unordered_set<std::string> binding_names;
    for (const auto &binding : bindings)
    {
        ASSERT_TRUE(binding_names.insert(binding.canonical_name).second);
    }
    ASSERT_EQ(bindings.size(), types.size());
    for (const auto &type : types)
    {
        EXPECT_EQ(binding_names.count(type.name), 1U);
    }
}

TEST(GameplayEditorBridgeTest, KeepsUnreflectedComponentsVisibleWithoutPublishingRtti)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());

    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<UnreflectedComponent>();
    ASSERT_NE(component, nullptr);

    bridge.PublishSnapshot();
    const auto snapshot = bridge.GetLatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->actors.size(), 1U);
    ASSERT_EQ(snapshot->actors.front().components.size(), 1U);
    const auto &component_snapshot = snapshot->actors.front().components.front();
    EXPECT_EQ(component_snapshot.component, component->GetInstanceId());
    EXPECT_FALSE(component_snapshot.type.IsValid());
    EXPECT_TRUE(component_snapshot.properties.empty());
    EXPECT_FALSE(component_snapshot.diagnostic.empty());
}

TEST(GameplayEditorBridgeTest, AppliesEditsOnTheGameThreadAndReadsBackAcceptedValue)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());

    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const second_component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(component, nullptr);
    ASSERT_NE(second_component, nullptr);
    const auto *const type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.SceneComponent");
    ASSERT_NE(type, nullptr);
    const auto *const property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.SceneComponent",
        "transform.location.x");
    ASSERT_NE(property, nullptr);

    const auto submission = bridge.SubmitPropertyEdit({
        1U, actor_handle, component->GetInstanceId(), type->id, property->id,
        kpengine::reflection::ReflectionValue{12.5f}});
    ASSERT_TRUE(submission.IsQueued());
    bridge.PumpEdits();

    const auto results = bridge.ConsumeEditResults();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().status,
              kpengine::gameplay::PropertyEditResultStatus::Applied);
    ASSERT_NE(results.front().value.TryGet<double>(), nullptr);
    EXPECT_DOUBLE_EQ(*results.front().value.TryGet<double>(), 12.5);
    EXPECT_FLOAT_EQ(component->GetLocalLocation().x_, 12.5f);
    EXPECT_FLOAT_EQ(second_component->GetLocalLocation().x_, 0.0f);

    bridge.PublishSnapshot();
    const auto snapshot = bridge.GetLatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    const auto &properties = snapshot->actors.front().components.front().properties;
    const auto it = std::find_if(properties.begin(), properties.end(),
                                 [property](const auto &value) {
                                     return value.property == property->id;
                                 });
    ASSERT_NE(it, properties.end());
    ASSERT_NE(it->value.TryGet<double>(), nullptr);
    EXPECT_DOUBLE_EQ(*it->value.TryGet<double>(), 12.5);
}

TEST(GameplayEditorBridgeTest, AppliesDeterministicPrefixBudgets)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridgeConfig config;
    config.max_actors = 1;
    config.max_components = 1;
    config.max_properties = 1;
    config.max_value_bytes = 1;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
    ASSERT_TRUE(bridge.Initialize());

    const auto first_handle = fixture.world.CreateActor();
    const auto second_handle = fixture.world.CreateActor();
    auto *const first_actor = fixture.world.FindActor(first_handle);
    auto *const second_actor = fixture.world.FindActor(second_handle);
    ASSERT_NE(first_actor, nullptr);
    ASSERT_NE(second_actor, nullptr);
    ASSERT_NE(first_actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);
    ASSERT_NE(second_actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);

    bridge.PublishSnapshot();
    const auto snapshot = bridge.GetLatestSnapshot();
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->actors.size(), 1U);
    EXPECT_TRUE(snapshot->truncated);
    EXPECT_EQ(snapshot->omitted.actors, 1U);
    EXPECT_EQ(snapshot->omitted.components, 1U);
    EXPECT_GT(snapshot->omitted.properties, 0U);
    EXPECT_GT(snapshot->omitted.value_bytes, 0U);
}

TEST(GameplayEditorBridgeTest, ReturnsDeterministicRejectionStatuses)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());

    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const scene = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const camera = actor->AddComponent<kpengine::gameplay::CameraComponent>();
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(camera, nullptr);

    const auto *const scene_type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.SceneComponent");
    const auto *const mesh_type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.MeshComponent");
    const auto *const camera_type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.CameraComponent");
    const auto *const location_property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.SceneComponent",
        "transform.location.x");
    const auto *const field_of_view_property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.CameraComponent",
        "camera.field_of_view");
    ASSERT_NE(scene_type, nullptr);
    ASSERT_NE(mesh_type, nullptr);
    ASSERT_NE(camera_type, nullptr);
    ASSERT_NE(location_property, nullptr);
    ASSERT_NE(field_of_view_property, nullptr);

    EXPECT_TRUE(bridge.SubmitPropertyEdit({
                         30U, actor_handle, {999U}, scene_type->id, location_property->id,
                         kpengine::reflection::ReflectionValue{1.0f}})
                    .IsQueued());
    EXPECT_TRUE(bridge.SubmitPropertyEdit({
                         31U, actor_handle, scene->GetInstanceId(), mesh_type->id,
                         location_property->id, kpengine::reflection::ReflectionValue{1.0f}})
                    .IsQueued());
    EXPECT_TRUE(bridge.SubmitPropertyEdit({
                         32U, actor_handle, scene->GetInstanceId(), scene_type->id,
                         {0xdeadbeefu}, kpengine::reflection::ReflectionValue{1.0f}})
                    .IsQueued());
    EXPECT_TRUE(bridge.SubmitPropertyEdit({
                         33U, actor_handle, scene->GetInstanceId(), scene_type->id,
                         location_property->id, kpengine::reflection::ReflectionValue{"bad"}})
                    .IsQueued());
    EXPECT_TRUE(bridge.SubmitPropertyEdit({
                         34U, actor_handle, camera->GetInstanceId(), camera_type->id,
                         field_of_view_property->id, kpengine::reflection::ReflectionValue{0.0f}})
                    .IsQueued());

    bridge.PumpEdits();
    const auto results = bridge.ConsumeEditResults();
    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[0].status, kpengine::gameplay::PropertyEditResultStatus::StaleComponent);
    EXPECT_EQ(results[1].status,
              kpengine::gameplay::PropertyEditResultStatus::ReflectedTypeMismatch);
    EXPECT_EQ(results[2].status, kpengine::gameplay::PropertyEditResultStatus::UnknownProperty);
    EXPECT_EQ(results[3].status, kpengine::gameplay::PropertyEditResultStatus::ValueRejected);
    EXPECT_EQ(results[4].status, kpengine::gameplay::PropertyEditResultStatus::ValueRejected);
    EXPECT_EQ(results[3].reflection_status,
              kpengine::reflection::ReflectionResultStatus::TypeMismatch);
    EXPECT_EQ(results[4].reflection_status,
              kpengine::reflection::ReflectionResultStatus::SetterRejected);
}

TEST(GameplayEditorBridgeTest, AppliesIndependentSnapshotBudgets)
{
    ReflectionFixture fixture;
    const auto first_handle = fixture.world.CreateActor();
    const auto second_handle = fixture.world.CreateActor();
    auto *const first_actor = fixture.world.FindActor(first_handle);
    auto *const second_actor = fixture.world.FindActor(second_handle);
    ASSERT_NE(first_actor, nullptr);
    ASSERT_NE(second_actor, nullptr);
    ASSERT_NE(first_actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);
    ASSERT_NE(first_actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);
    ASSERT_NE(second_actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);

    {
        kpengine::gameplay::GameplayEditorBridgeConfig config;
        config.max_actors = 1;
        kpengine::gameplay::GameplayEditorBridge bridge(
            fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
        ASSERT_TRUE(bridge.Initialize());
        bridge.PublishSnapshot();
        const auto snapshot = bridge.GetLatestSnapshot();
        ASSERT_NE(snapshot, nullptr);
        EXPECT_EQ(snapshot->actors.size(), 1U);
        EXPECT_EQ(snapshot->omitted.actors, 1U);
    }
    {
        kpengine::gameplay::GameplayEditorBridgeConfig config;
        config.max_components = 1;
        kpengine::gameplay::GameplayEditorBridge bridge(
            fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
        ASSERT_TRUE(bridge.Initialize());
        bridge.PublishSnapshot();
        const auto snapshot = bridge.GetLatestSnapshot();
        ASSERT_NE(snapshot, nullptr);
        EXPECT_EQ(snapshot->omitted.components, 2U);
    }
    {
        kpengine::gameplay::GameplayEditorBridgeConfig config;
        config.max_properties = 1;
        kpengine::gameplay::GameplayEditorBridge bridge(
            fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
        ASSERT_TRUE(bridge.Initialize());
        bridge.PublishSnapshot();
        const auto snapshot = bridge.GetLatestSnapshot();
        ASSERT_NE(snapshot, nullptr);
        EXPECT_GT(snapshot->omitted.properties, 0U);
    }
    {
        kpengine::gameplay::GameplayEditorBridgeConfig config;
        config.max_value_bytes = 1;
        kpengine::gameplay::GameplayEditorBridge bridge(
            fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
        ASSERT_TRUE(bridge.Initialize());
        bridge.PublishSnapshot();
        const auto snapshot = bridge.GetLatestSnapshot();
        ASSERT_NE(snapshot, nullptr);
        EXPECT_GT(snapshot->omitted.value_bytes, 0U);
    }
}

TEST(GameplayEditorBridgeTest, RejectsReadOnlyPropertiesBeforeReflectionWrite)
{
    ReflectionFixture fixture;
    ReadOnlyCatalogView catalog(*fixture.system.GetCatalog());
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, catalog, *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());

    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(component, nullptr);
    const auto *const type = catalog.FindType("kpengine.gameplay.SceneComponent");
    const auto *const property = FindProperty(
        catalog, "kpengine.gameplay.SceneComponent", "transform.location.x");
    ASSERT_NE(type, nullptr);
    ASSERT_NE(property, nullptr);

    ASSERT_TRUE(bridge.SubmitPropertyEdit({
                         35U, actor_handle, component->GetInstanceId(), type->id, property->id,
                         kpengine::reflection::ReflectionValue{2.0f}})
                    .IsQueued());
    bridge.PumpEdits();
    const auto results = bridge.ConsumeEditResults();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().status, kpengine::gameplay::PropertyEditResultStatus::NotWritable);
    EXPECT_EQ(results.front().reflection_status,
              kpengine::reflection::ReflectionResultStatus::ReadOnly);
    EXPECT_FLOAT_EQ(component->GetLocalLocation().x_, 0.0f);
}

TEST(GameplayEditorBridgeTest, ConcurrentSnapshotLoadsSeeCompleteRevisions)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());
    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->AddComponent<kpengine::gameplay::SceneComponent>(), nullptr);

    std::atomic_bool stop{false};
    std::atomic_bool valid{true};
    std::thread reader([&] {
        uint64_t last_revision = 0;
        while (!stop.load(std::memory_order_acquire))
        {
            const auto snapshot = bridge.GetLatestSnapshot();
            if (snapshot != nullptr)
            {
                if (snapshot->revision < last_revision || snapshot->actors.size() != 1U ||
                    snapshot->actors.front().components.size() != 1U)
                {
                    valid.store(false, std::memory_order_release);
                    return;
                }
                last_revision = snapshot->revision;
            }
        }
    });

    for (int index = 0; index < 100; ++index)
    {
        bridge.PublishSnapshot();
    }
    stop.store(true, std::memory_order_release);
    reader.join();
    EXPECT_TRUE(valid.load(std::memory_order_acquire));
}

TEST(GameplayEditorBridgeTest, RejectsStaleActorsAndPreservesQueueBackpressure)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridgeConfig config;
    config.max_outstanding_edits = 2;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess(), config);
    ASSERT_TRUE(bridge.Initialize());

    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(component, nullptr);
    const auto *const type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.SceneComponent");
    const auto *const property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.SceneComponent",
        "transform.location.x");
    ASSERT_NE(type, nullptr);
    ASSERT_NE(property, nullptr);

    const kpengine::gameplay::PropertyEditCommand command{
        7U, actor_handle, component->GetInstanceId(), type->id, property->id,
        kpengine::reflection::ReflectionValue{3.0f}};
    EXPECT_TRUE(bridge.SubmitPropertyEdit(command).IsQueued());
    EXPECT_EQ(bridge.SubmitPropertyEdit(command).status,
              kpengine::gameplay::PropertyEditSubmissionStatus::InvalidArgument);
    auto second_command = command;
    second_command.request_id = 8U;
    EXPECT_TRUE(bridge.SubmitPropertyEdit(second_command).IsQueued());
    auto third_command = command;
    third_command.request_id = 9U;
    EXPECT_EQ(bridge.SubmitPropertyEdit(third_command).status,
              kpengine::gameplay::PropertyEditSubmissionStatus::QueueFull);

    fixture.world.DestroyActor(actor_handle);
    fixture.world.ReclaimDestroyedActors();
    const auto reused_handle = fixture.world.CreateActor();
    EXPECT_EQ(reused_handle.id, actor_handle.id);
    EXPECT_NE(reused_handle.generation, actor_handle.generation);
    bridge.PumpEdits();
    const auto results = bridge.ConsumeEditResults();
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, kpengine::gameplay::PropertyEditResultStatus::StaleActor);
    EXPECT_EQ(results[1].status, kpengine::gameplay::PropertyEditResultStatus::StaleActor);
}

TEST(GameplayEditorBridgeTest, WrongThreadPumpDoesNotAccessGameplay)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());
    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(component, nullptr);
    const auto *const type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.SceneComponent");
    const auto *const property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.SceneComponent",
        "transform.location.x");
    ASSERT_NE(type, nullptr);
    ASSERT_NE(property, nullptr);

    ASSERT_TRUE(bridge.SubmitPropertyEdit({
        12U, actor_handle, component->GetInstanceId(), type->id, property->id,
        kpengine::reflection::ReflectionValue{9.0f}})
                    .IsQueued());
    std::thread worker([&bridge] { bridge.PumpEdits(); });
    worker.join();
    EXPECT_TRUE(bridge.ConsumeEditResults().empty());
    bridge.PumpEdits();
    EXPECT_EQ(bridge.ConsumeEditResults().size(), 1U);
}

TEST(GameplayEditorBridgeTest, ShutdownCancelsPendingEditsAndClearsSnapshot)
{
    ReflectionFixture fixture;
    kpengine::gameplay::GameplayEditorBridge bridge(
        fixture.world, *fixture.system.GetCatalog(), *fixture.system.GetAccess());
    ASSERT_TRUE(bridge.Initialize());
    const auto actor_handle = fixture.world.CreateActor();
    auto *const actor = fixture.world.FindActor(actor_handle);
    ASSERT_NE(actor, nullptr);
    auto *const component = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(component, nullptr);
    const auto *const type = fixture.system.GetCatalog()->FindType(
        "kpengine.gameplay.SceneComponent");
    const auto *const property = FindProperty(
        *fixture.system.GetCatalog(), "kpengine.gameplay.SceneComponent",
        "transform.location.x");
    ASSERT_NE(type, nullptr);
    ASSERT_NE(property, nullptr);

    ASSERT_TRUE(bridge.SubmitPropertyEdit({
        20U, actor_handle, component->GetInstanceId(), type->id, property->id,
        kpengine::reflection::ReflectionValue{1.0f}})
                    .IsQueued());
    bridge.PublishSnapshot();
    bridge.Shutdown();
    bridge.Shutdown();
    EXPECT_EQ(bridge.GetLatestSnapshot(), nullptr);
    const auto results = bridge.ConsumeEditResults();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().status,
              kpengine::gameplay::PropertyEditResultStatus::CancelledByShutdown);
    EXPECT_EQ(bridge.SubmitPropertyEdit({
                  21U, actor_handle, component->GetInstanceId(), type->id, property->id,
                  kpengine::reflection::ReflectionValue{2.0f}})
                  .status,
              kpengine::gameplay::PropertyEditSubmissionStatus::Stopped);
}
