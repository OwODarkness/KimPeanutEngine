#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gameplay/actor/actor.h"
#include "gameplay/component/actor_component.h"
#include "gameplay/component/camera_component.h"
#include "gameplay/component/directional_light_component.h"
#include "gameplay/component/point_light_component.h"
#include "gameplay/component/spot_light_component.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/component/primitive_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/controller/player_controller.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/free_camera_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "input/input_context.h"
#include "input/input_key.h"
#include "input/input_system.h"

namespace
{
    class RecordingComponent final : public kpengine::gameplay::ActorComponent
    {
    public:
        RecordingComponent(std::vector<std::string> &events, std::string name)
            : events_(&events), name_(std::move(name))
        {
        }

    protected:
        void OnInitialize() override { events_->push_back("initialize:" + name_); }
        void OnActivate() override { events_->push_back("activate:" + name_); }
        void OnDeactivate() override { events_->push_back("deactivate:" + name_); }
        void OnTick(float delta_time) override
        {
            events_->push_back("tick:" + name_ + ":" + std::to_string(delta_time));
        }

    private:
        std::vector<std::string> *events_ = nullptr;
        std::string name_;
    };

    class RecordingSourceSink final : public kpengine::render::IRenderableSourceSink
    {
    public:
        kpengine::render::RenderableSourceHandle EnqueueCreate(
            const kpengine::render::PrimitiveRenderableSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(kpengine::render::RenderableSourceHandle handle,
                           const kpengine::render::PrimitiveRenderableSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(kpengine::render::RenderableSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            kpengine::render::RenderableSourceHandle handle;
            kpengine::render::PrimitiveRenderableSourceDesc source;
        };

        uint32_t next_id = 0;
        std::vector<kpengine::render::PrimitiveRenderableSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<kpengine::render::RenderableSourceHandle> destroys;
    };

    class RecordingLightSourceSink final : public kpengine::render::ILightSourceSink
    {
    public:
        kpengine::render::LightSourceHandle EnqueueCreate(
            const kpengine::render::LightSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(kpengine::render::LightSourceHandle handle,
                           const kpengine::render::LightSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(kpengine::render::LightSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            kpengine::render::LightSourceHandle handle;
            kpengine::render::LightSourceDesc source;
        };

        uint32_t next_id = 0;
        std::vector<kpengine::render::LightSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<kpengine::render::LightSourceHandle> destroys;
    };

    class RecordingCameraSourceSink final : public kpengine::render::ICameraSourceSink
    {
    public:
        kpengine::render::CameraSourceHandle EnqueueCreate(
            const kpengine::render::CameraSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(kpengine::render::CameraSourceHandle handle,
                           const kpengine::render::CameraSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(kpengine::render::CameraSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            kpengine::render::CameraSourceHandle handle;
            kpengine::render::CameraSourceDesc source;
        };

        uint32_t next_id = 0;
        std::vector<kpengine::render::CameraSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<kpengine::render::CameraSourceHandle> destroys;
    };
}

TEST(GameplayWorldTest, RunsComponentLifecycleInDocumentedOrder)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    std::vector<std::string> events;
    ASSERT_NE(actor->AddComponent<RecordingComponent>(events, "first"), nullptr);
    ASSERT_NE(actor->AddComponent<RecordingComponent>(events, "second"), nullptr);

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    world.Tick(0.5f);
    ASSERT_TRUE(world.DeactivateActor(handle));

    const std::vector<std::string> expected{
        "initialize:first", "initialize:second", "activate:first", "activate:second",
        "tick:first:0.500000", "tick:second:0.500000", "deactivate:second",
        "deactivate:first"};
    EXPECT_EQ(events, expected);
}

TEST(GameplayWorldTest, AllowsDuplicateComponentsButRejectsLateAddition)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    std::vector<std::string> events;
    EXPECT_NE(actor->AddComponent<RecordingComponent>(events, "first"), nullptr);
    EXPECT_NE(actor->AddComponent<RecordingComponent>(events, "second"), nullptr);
    ASSERT_TRUE(world.InitializeActor(handle));
    EXPECT_EQ(actor->AddComponent<RecordingComponent>(events, "late"), nullptr);
}

TEST(GameplayWorldTest, DestroyInvalidatesHandleAndDeactivatesBeforeReclaim)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    std::vector<std::string> events;
    ASSERT_NE(actor->AddComponent<RecordingComponent>(events, "only"), nullptr);
    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    ASSERT_TRUE(world.DestroyActor(handle));

    EXPECT_EQ(world.FindActor(handle), nullptr);
    EXPECT_FALSE(world.DestroyActor(handle));
    ASSERT_EQ(events, (std::vector<std::string>{"initialize:only", "activate:only",
                                                 "deactivate:only"}));

    world.Tick(0.25f);
    const kpengine::gameplay::ActorHandle replacement = world.CreateActor();
    EXPECT_EQ(replacement.id, handle.id);
    EXPECT_NE(replacement.generation, handle.generation);

    world.Clear();
    EXPECT_EQ(world.FindActor(replacement), nullptr);
    const kpengine::gameplay::ActorHandle after_clear = world.CreateActor();
    EXPECT_EQ(after_clear.id, replacement.id);
    EXPECT_NE(after_clear.generation, replacement.generation);
}

TEST(GameplayWorldTest, PropagatesAttachedSceneTransformsAndSupportsDetach)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    auto *const parent = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const child = actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(parent));
    parent->SetLocalLocation({10.0f, 0.0f, 0.0f});
    child->SetLocalLocation({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(child->AttachTo(*parent));

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    world.Tick(0.0f);
    EXPECT_EQ(child->GetWorldLocation(), (kpengine::Vector3f{11.0f, 0.0f, 0.0f}));

    parent->SetLocalTransform({{20.0f, 0.0f, 0.0f}, {0.0f, 90.0f, 0.0f},
                               {2.0f, 2.0f, 2.0f}});
    EXPECT_TRUE(parent->IsTransformDirty());
    EXPECT_TRUE(child->IsTransformDirty());
    world.Tick(0.0f);
    EXPECT_NEAR(child->GetWorldLocation().x_, 20.0f, 0.0001f);
    EXPECT_NEAR(child->GetWorldLocation().y_, 0.0f, 0.0001f);
    EXPECT_NEAR(child->GetWorldLocation().z_, -2.0f, 0.0001f);

    ASSERT_TRUE(child->Detach());
    world.Tick(0.0f);
    EXPECT_EQ(child->GetWorldLocation(), (kpengine::Vector3f{1.0f, 0.0f, 0.0f}));
}

TEST(GameplayWorldTest, RejectsInvalidAndCrossActorAttachments)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle first_handle = world.CreateActor();
    const kpengine::gameplay::ActorHandle second_handle = world.CreateActor();
    auto *const first_actor = world.FindActor(first_handle);
    auto *const second_actor = world.FindActor(second_handle);
    ASSERT_NE(first_actor, nullptr);
    ASSERT_NE(second_actor, nullptr);

    auto *const root = first_actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const child = first_actor->AddComponent<kpengine::gameplay::SceneComponent>();
    auto *const other = second_actor->AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_TRUE(child->AttachTo(*root));
    EXPECT_FALSE(root->AttachTo(*child));
    EXPECT_FALSE(root->AttachTo(*root));
    EXPECT_FALSE(child->AttachTo(*other));
}

TEST(GameplayWorldTest, ComputesPrimitiveWorldBoundsAndKeepsPrimitiveFlagsHeadless)
{
    kpengine::gameplay::GameplayWorld world{};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    auto *const primitive = actor->AddComponent<kpengine::gameplay::PrimitiveComponent>();
    ASSERT_NE(primitive, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(primitive));
    primitive->SetLocalLocation({10.0f, 0.0f, 0.0f});
    primitive->SetLocalScale({2.0f, 2.0f, 2.0f});
    primitive->SetLocalBounds({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
    primitive->SetVisible(false);
    primitive->SetCastsShadow(false);

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    world.Tick(0.0f);

    EXPECT_FALSE(primitive->IsVisible());
    EXPECT_FALSE(primitive->CastsShadow());
    EXPECT_EQ(primitive->GetWorldBounds(),
              (kpengine::spatial::AABB{{8.0f, -2.0f, -2.0f}, {12.0f, 2.0f, 2.0f}}));
}

TEST(GameplayWorldTest, MeshComponentPublishesCoalescedSourceLifecycle)
{
    RecordingSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    auto *const mesh = actor->AddComponent<kpengine::gameplay::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(mesh));
    mesh->SetMeshAsset({42, 3, kpengine::asset::AssetType::KPAT_Mesh});
    mesh->SetMaterialAsset({7, 2, kpengine::asset::AssetType::KPAT_Material});
    mesh->SetLocalBounds({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    ASSERT_EQ(source_sink.creates.size(), 1U);
    EXPECT_TRUE(mesh->GetSourceHandle().IsValid());

    const auto &created = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(
        source_sink.creates.front());
    EXPECT_EQ(created.mesh_asset, (kpengine::asset::AssetID{42, 3, kpengine::asset::AssetType::KPAT_Mesh}));
    EXPECT_EQ(created.material_asset,
              (kpengine::asset::AssetID{7, 2, kpengine::asset::AssetType::KPAT_Material}));
    EXPECT_TRUE(created.flags.visible);
    EXPECT_TRUE(created.flags.casts_shadow);
    EXPECT_EQ(created.local_bounds,
              (kpengine::spatial::AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}));

    mesh->SetLocalLocation({2.0f, 0.0f, 0.0f});
    mesh->SetLocalLocation({4.0f, 0.0f, 0.0f});
    mesh->SetVisible(false);
    mesh->SetCastsShadow(false);
    mesh->SetLodBias(2);
    world.Tick(0.0f);

    ASSERT_EQ(source_sink.updates.size(), 1U);
    const auto &updated = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(
        source_sink.updates.front().source);
    EXPECT_EQ(source_sink.updates.front().handle, mesh->GetSourceHandle());
    EXPECT_EQ(updated.world_transform.position_, (kpengine::Vector3f{4.0f, 0.0f, 0.0f}));
    EXPECT_EQ(updated.world_bounds,
              (kpengine::spatial::AABB{{3.0f, -1.0f, -1.0f}, {5.0f, 1.0f, 1.0f}}));
    EXPECT_EQ(updated.local_bounds,
              (kpengine::spatial::AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}));
    EXPECT_FALSE(updated.flags.visible);
    EXPECT_FALSE(updated.flags.casts_shadow);
    EXPECT_EQ(updated.lod_bias, 2);

    ASSERT_TRUE(world.DestroyActor(handle));
    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(source_sink.destroys.front(), source_sink.updates.front().handle);
    EXPECT_FALSE(mesh->GetSourceHandle().IsValid());
}

TEST(GameplayWorldTest, WorldTeardownDeactivatesActiveMeshComponents)
{
    RecordingSourceSink source_sink{};
    {
        kpengine::gameplay::GameplayWorld world{&source_sink};
        const kpengine::gameplay::ActorHandle handle = world.CreateActor();
        kpengine::gameplay::Actor *const actor = world.FindActor(handle);
        ASSERT_NE(actor, nullptr);

        auto *const mesh = actor->AddComponent<kpengine::gameplay::MeshComponent>();
        ASSERT_NE(mesh, nullptr);
        ASSERT_TRUE(world.InitializeActor(handle));
        ASSERT_TRUE(world.ActivateActor(handle));
        ASSERT_EQ(source_sink.creates.size(), 1U);
    }

    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(source_sink.destroys.front(), (kpengine::render::RenderableSourceHandle{0, 0}));
}

TEST(GameplayWorldTest, DirectionalLightComponentPublishesCoalescedSourceLifecycle)
{
    RecordingLightSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{nullptr, &source_sink};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    auto *const light = actor->AddComponent<kpengine::gameplay::DirectionalLightComponent>();
    ASSERT_NE(light, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(light));
    light->SetDirection({0.0f, -1.0f, 0.0f});
    light->SetColor({1.0f, 0.5f, 0.25f});
    light->SetIntensity(3.0f);

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    ASSERT_EQ(source_sink.creates.size(), 1U);
    EXPECT_TRUE(light->GetSourceHandle().IsValid());

    const auto &created =
        std::get<kpengine::render::DirectionalLightSourceDesc>(source_sink.creates.front());
    EXPECT_EQ(created.direction, (kpengine::Vector3f{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ(created.color, (kpengine::Vector3f{1.0f, 0.5f, 0.25f}));
    EXPECT_EQ(created.intensity, 3.0f);
    EXPECT_TRUE(created.enabled);

    light->SetDirection({1.0f, -1.0f, 0.0f});
    light->SetDirection({0.0f, 0.0f, -1.0f});
    light->SetIntensity(5.0f);
    light->SetLightEnabled(false);
    world.Tick(0.0f);

    ASSERT_EQ(source_sink.updates.size(), 1U);
    EXPECT_EQ(source_sink.updates.front().handle, light->GetSourceHandle());
    const auto &updated = std::get<kpengine::render::DirectionalLightSourceDesc>(
        source_sink.updates.front().source);
    EXPECT_EQ(updated.direction, (kpengine::Vector3f{0.0f, 0.0f, -1.0f}));
    EXPECT_EQ(updated.intensity, 5.0f);
    EXPECT_FALSE(updated.enabled);

    ASSERT_TRUE(world.DestroyActor(handle));
    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(source_sink.destroys.front(), source_sink.updates.front().handle);
    EXPECT_FALSE(light->GetSourceHandle().IsValid());
}

TEST(GameplayWorldTest, CameraComponentPublishesValidatedViewStateAndBasis)
{
    RecordingCameraSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{nullptr, nullptr, &source_sink};
    const kpengine::gameplay::ActorHandle handle = world.CreateActor();
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);

    auto *const camera = actor->AddComponent<kpengine::gameplay::CameraComponent>();
    ASSERT_NE(camera, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(camera));
    camera->SetLocalTransform({{2.0f, 3.0f, 4.0f}, {0.0f, -90.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    camera->SetFieldOfView(60.0f);
    camera->SetNearPlane(1.0f);
    camera->SetFarPlane(500.0f);
    camera->SetProjectionMode(kpengine::render::CameraProjectionMode::Orthographic);
    camera->SetOrthographicHeight(20.0f);
    camera->SetPriority(3);

    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));
    ASSERT_EQ(source_sink.creates.size(), 1U);
    EXPECT_NEAR(camera->GetForward().x_, 0.0f, 0.0001f);
    EXPECT_NEAR(camera->GetForward().y_, 0.0f, 0.0001f);
    EXPECT_NEAR(camera->GetForward().z_, -1.0f, 0.0001f);
    EXPECT_NEAR(camera->GetRight().x_, 1.0f, 0.0001f);
    EXPECT_NEAR(camera->GetRight().y_, 0.0f, 0.0001f);
    EXPECT_NEAR(camera->GetRight().z_, 0.0f, 0.0001f);
    EXPECT_NEAR(camera->GetUp().x_, 0.0f, 0.0001f);
    EXPECT_NEAR(camera->GetUp().y_, 1.0f, 0.0001f);
    EXPECT_NEAR(camera->GetUp().z_, 0.0f, 0.0001f);

    const auto &created = source_sink.creates.front();
    EXPECT_EQ(created.world_transform.position_, (kpengine::Vector3f{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(created.projection_mode, kpengine::render::CameraProjectionMode::Orthographic);
    EXPECT_FLOAT_EQ(created.field_of_view_degrees, 60.0f);
    EXPECT_FLOAT_EQ(created.near_plane, 1.0f);
    EXPECT_FLOAT_EQ(created.far_plane, 500.0f);
    EXPECT_FLOAT_EQ(created.orthographic_height, 20.0f);
    EXPECT_EQ(created.priority, 3);

    camera->SetFieldOfView(0.0f);
    camera->SetNearPlane(500.0f);
    camera->SetFarPlane(0.5f);
    EXPECT_FLOAT_EQ(camera->GetFieldOfView(), 60.0f);
    EXPECT_FLOAT_EQ(camera->GetNearPlane(), 1.0f);
    EXPECT_FLOAT_EQ(camera->GetFarPlane(), 500.0f);

    camera->SetLocalLocation({8.0f, 3.0f, 4.0f});
    camera->SetCameraEnabled(false);
    world.Tick(0.0f);
    ASSERT_EQ(source_sink.updates.size(), 1U);
    EXPECT_EQ(source_sink.updates.front().handle, camera->GetSourceHandle());
    EXPECT_EQ(source_sink.updates.front().source.world_transform.position_,
              (kpengine::Vector3f{8.0f, 3.0f, 4.0f}));
    EXPECT_FALSE(source_sink.updates.front().source.enabled);

    ASSERT_TRUE(world.DestroyActor(handle));
    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_FALSE(camera->GetSourceHandle().IsValid());
}

TEST(GameplayWorldTest, DirectionalLightActorFactoryBuildsAnActiveLightComposition)
{
    RecordingLightSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{nullptr, &source_sink};
    const kpengine::gameplay::DirectionalLightActorDesc desc{
        {0.0f, -0.5f, -0.5f}, {0.75f, 0.5f, 0.25f}, 4.0f, false};

    const kpengine::gameplay::ActorHandle handle =
        kpengine::gameplay::CreateDirectionalLightActor(world, desc);
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetState(), kpengine::gameplay::ActorState::Active);
    auto *const light = actor->FindComponent<kpengine::gameplay::DirectionalLightComponent>();
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(actor->GetRootComponent(), light);
    ASSERT_EQ(source_sink.creates.size(), 1U);

    const auto &source =
        std::get<kpengine::render::DirectionalLightSourceDesc>(source_sink.creates.front());
    EXPECT_EQ(source.direction, desc.direction);
    EXPECT_EQ(source.color, desc.color);
    EXPECT_EQ(source.intensity, desc.intensity);
    EXPECT_EQ(source.enabled, desc.enabled);
    EXPECT_EQ(source.casts_shadow, desc.casts_shadow);
}

TEST(GameplayWorldTest, PointLightActorFactoryPublishesUnshadowedPointSource)
{
    RecordingLightSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{nullptr, &source_sink};
    const kpengine::gameplay::PointLightActorDesc desc{
        {3.0f, 4.0f, 5.0f}, {0.25f, 0.5f, 0.75f}, 12.0f, 30.0f, true};

    const kpengine::gameplay::ActorHandle handle =
        kpengine::gameplay::CreatePointLightActor(world, desc);
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetState(), kpengine::gameplay::ActorState::Active);
    auto *const light = actor->FindComponent<kpengine::gameplay::PointLightComponent>();
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(actor->GetRootComponent(), light);
    ASSERT_EQ(source_sink.creates.size(), 1U);

    const auto &source =
        std::get<kpengine::render::PointLightSourceDesc>(source_sink.creates.front());
    EXPECT_EQ(source.position, desc.position);
    EXPECT_EQ(source.color, desc.color);
    EXPECT_EQ(source.intensity, desc.intensity);
    EXPECT_EQ(source.range, desc.range);
    EXPECT_EQ(source.enabled, desc.enabled);
    EXPECT_EQ(source.casts_shadow, desc.casts_shadow);

    light->SetRange(48.0f);
    world.Tick(1.0f / 60.0f);
    ASSERT_EQ(source_sink.updates.size(), 1U);
    const auto &updated =
        std::get<kpengine::render::PointLightSourceDesc>(source_sink.updates.front().source);
    EXPECT_EQ(updated.range, 48.0f);
}

TEST(GameplayWorldTest, SpotLightActorFactoryPublishesUnshadowedSpotSource)
{
    RecordingLightSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{nullptr, &source_sink};
    const kpengine::gameplay::SpotLightActorDesc desc{
        {3.0f, 4.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {0.25f, 0.5f, 0.75f},
        12.0f, 30.0f, 0.2f, 0.7f, true};

    const kpengine::gameplay::ActorHandle handle =
        kpengine::gameplay::CreateSpotLightActor(world, desc);
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetState(), kpengine::gameplay::ActorState::Active);
    auto *const light = actor->FindComponent<kpengine::gameplay::SpotLightComponent>();
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(actor->GetRootComponent(), light);
    ASSERT_EQ(source_sink.creates.size(), 1U);

    const auto &source =
        std::get<kpengine::render::SpotLightSourceDesc>(source_sink.creates.front());
    EXPECT_EQ(source.position, desc.position);
    EXPECT_EQ(source.direction, desc.direction);
    EXPECT_EQ(source.color, desc.color);
    EXPECT_EQ(source.intensity, desc.intensity);
    EXPECT_EQ(source.range, desc.range);
    EXPECT_EQ(source.inner_cone_radians, desc.inner_cone_radians);
    EXPECT_EQ(source.outer_cone_radians, desc.outer_cone_radians);
    EXPECT_EQ(source.enabled, desc.enabled);

    light->SetOuterConeRadians(0.9f);
    world.Tick(1.0f / 60.0f);
    ASSERT_EQ(source_sink.updates.size(), 1U);
    const auto &updated =
        std::get<kpengine::render::SpotLightSourceDesc>(source_sink.updates.front().source);
    EXPECT_EQ(updated.outer_cone_radians, 0.9f);
}

TEST(GameplayWorldTest, StaticMeshActorFactoryBuildsAnActiveMeshComposition)
{
    RecordingSourceSink source_sink{};
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const kpengine::gameplay::StaticMeshActorDesc desc{
        {9, 1, kpengine::asset::AssetType::KPAT_Mesh},
        {6, 2, kpengine::asset::AssetType::KPAT_Material},
        {{3.0f, 0.0f, 0.0f}, {}, {2.0f, 2.0f, 2.0f}},
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},
        false,
        false,
        3};

    const kpengine::gameplay::ActorHandle handle =
        kpengine::gameplay::CreateStaticMeshActor(world, desc);
    kpengine::gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetState(), kpengine::gameplay::ActorState::Active);
    auto *const mesh = actor->FindComponent<kpengine::gameplay::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(actor->GetRootComponent(), mesh);
    ASSERT_EQ(source_sink.creates.size(), 1U);

    const auto &source = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(
        source_sink.creates.front());
    EXPECT_EQ(source.mesh_asset, desc.mesh_asset);
    EXPECT_EQ(source.material_asset, desc.material_asset);
    EXPECT_EQ(source.world_transform, desc.transform);
    EXPECT_FALSE(source.flags.visible);
    EXPECT_FALSE(source.flags.casts_shadow);
    EXPECT_EQ(source.lod_bias, desc.lod_bias);
}

TEST(GameplayWorldTest, StaticMeshActorFactoryRejectsIncompleteDescription)
{
    kpengine::gameplay::GameplayWorld world{};
    EXPECT_FALSE(kpengine::gameplay::CreateStaticMeshActor(world, {}).IsValid());
}

TEST(GameplayWorldTest, LocalPlayerControllerPossessesAndMovesFreeCamera)
{
    RecordingCameraSourceSink source_sink{};
    kpengine::input::InputSystem input_system;
    input_system.Initialize();
    auto context = std::make_shared<kpengine::input::InputContext>();
    input_system.AddContext("Gameplay", context);
    input_system.SetActiveContext("Gameplay");

    {
        kpengine::gameplay::GameplayWorld world{nullptr, nullptr, &source_sink};
        const kpengine::gameplay::ActorHandle camera_handle =
            kpengine::gameplay::CreateFreeCameraActor(world, {});
        ASSERT_TRUE(camera_handle.IsValid());

        kpengine::gameplay::PlayerController *const controller =
            world.CreateLocalPlayerController(&input_system, "Gameplay");
        ASSERT_NE(controller, nullptr);
        ASSERT_TRUE(controller->Possess(camera_handle));

        context->ProcessKeyInput(
            {kpengine::input::InputDevice::Keyboard,
             static_cast<int>(kpengine::input::KeyboardKeyCode::W)},
            kpengine::input::InputTriggleType::Pressed, 0);
        world.Tick(0.5f);

        auto *const camera = world.FindActor(camera_handle)->FindComponent<
            kpengine::gameplay::CameraComponent>();
        ASSERT_NE(camera, nullptr);
        EXPECT_NEAR(camera->GetLocalLocation().z_, 250.0f, 0.0001f);

        context->ProcessKeyInput(
            {kpengine::input::InputDevice::Keyboard,
             static_cast<int>(kpengine::input::KeyboardKeyCode::W)},
            kpengine::input::InputTriggleType::Released, 0);
        context->ProcessAxis2DInput(
            {kpengine::input::InputDevice::Mouse, kpengine::input::kMouseCursorCode}, 10.0f,
            -5.0f);
        context->ProcessAxis1DInput(
            {kpengine::input::InputDevice::Mouse, kpengine::input::kMouseScrollCode}, 1.0f);
        world.Tick(0.0f);

        EXPECT_NEAR(camera->GetLocalTransform().rotator_.yaw_, 271.0f, 0.0001f);
        EXPECT_NEAR(camera->GetLocalTransform().rotator_.pitch_, 0.5f, 0.0001f);
        EXPECT_NEAR(camera->GetFieldOfView(), 43.0f, 0.0001f);

        context->ProcessAxis2DInput(
            {kpengine::input::InputDevice::Mouse, kpengine::input::kMouseCursorCode}, 0.0f,
            2000.0f);
        world.Tick(0.0f);
        EXPECT_NEAR(camera->GetLocalTransform().rotator_.pitch_, -89.0f, 0.0001f);

        const kpengine::gameplay::ActorHandle stale_camera_handle = camera_handle;
        EXPECT_TRUE(world.DestroyActor(camera_handle));
        EXPECT_FALSE(controller->Possess(stale_camera_handle));

        controller->Unpossess();
        EXPECT_FALSE(controller->GetPossessedActor().IsValid());
    }

    input_system.Shutdown();
}

TEST(GameplayWorldTest, LocalPlayerControllerStopsCameraInputWhenDisabled)
{
    kpengine::input::InputSystem input_system;
    input_system.Initialize();
    auto context = std::make_shared<kpengine::input::InputContext>();
    input_system.AddContext("Gameplay", context);
    input_system.SetActiveContext("Gameplay");

    {
        kpengine::gameplay::GameplayWorld world{};
        const kpengine::gameplay::ActorHandle camera_handle =
            kpengine::gameplay::CreateFreeCameraActor(world, {});
        ASSERT_TRUE(camera_handle.IsValid());

        kpengine::gameplay::PlayerController *const controller =
            world.CreateLocalPlayerController(&input_system, "Gameplay");
        ASSERT_NE(controller, nullptr);
        ASSERT_TRUE(controller->Possess(camera_handle));

        auto *const camera = world.FindActor(camera_handle)->FindComponent<
            kpengine::gameplay::CameraComponent>();
        ASSERT_NE(camera, nullptr);

        controller->SetInputEnabled(false);
        context->ProcessKeyInput(
            {kpengine::input::InputDevice::Keyboard,
             static_cast<int>(kpengine::input::KeyboardKeyCode::W)},
            kpengine::input::InputTriggleType::Pressed, 0);
        context->ProcessAxis2DInput(
            {kpengine::input::InputDevice::Mouse, kpengine::input::kMouseCursorCode}, 10.0f,
            -5.0f);
        context->ProcessAxis1DInput(
            {kpengine::input::InputDevice::Mouse, kpengine::input::kMouseScrollCode}, 1.0f);
        world.Tick(0.5f);

        EXPECT_NEAR(camera->GetLocalLocation().z_, 300.0f, 0.0001f);
        EXPECT_NEAR(camera->GetLocalTransform().rotator_.yaw_, -90.0f, 0.0001f);
        EXPECT_NEAR(camera->GetFieldOfView(), 45.0f, 0.0001f);

        controller->SetInputEnabled(true);
        context->ProcessKeyInput(
            {kpengine::input::InputDevice::Keyboard,
             static_cast<int>(kpengine::input::KeyboardKeyCode::W)},
            kpengine::input::InputTriggleType::Pressed, 0);
        world.Tick(0.5f);
        EXPECT_NEAR(camera->GetLocalLocation().z_, 250.0f, 0.0001f);
    }

    input_system.Shutdown();
}

TEST(GameplayWorldTest, LocalPlayerControllerConsumesGamepadSticksOnGameThread)
{
    RecordingCameraSourceSink source_sink{};
    kpengine::input::InputSystem input_system;
    input_system.Initialize();
    auto context = std::make_shared<kpengine::input::InputContext>();
    input_system.AddContext("Gameplay", context);
    input_system.SetActiveContext("Gameplay");
    kpengine::EventDispatcher<kpengine::GamepadStateEvent> dispatcher;
    input_system.BindGamepadEvent(dispatcher);

    {
        kpengine::gameplay::GameplayWorld world{nullptr, nullptr, &source_sink};
        const kpengine::gameplay::ActorHandle camera_handle =
            kpengine::gameplay::CreateFreeCameraActor(world, {});
        ASSERT_TRUE(camera_handle.IsValid());
        kpengine::gameplay::PlayerController *const controller =
            world.CreateLocalPlayerController(&input_system, "Gameplay");
        ASSERT_NE(controller, nullptr);
        ASSERT_TRUE(controller->Possess(camera_handle));
        controller->SetGamepadLookSensitivity(10.0f);

        kpengine::GamepadStateEvent sample{};
        sample.gamepad_index = 0;
        sample.connected = true;
        sample.axes[1] = -0.6f;
        dispatcher.Dispatch(sample);
        const kpengine::input::InputFrameSnapshot snapshot = input_system.ConsumeFrameSnapshot();
        ASSERT_EQ(snapshot.events.size(), 4U);
        for (const kpengine::input::InputActionEvent &event : snapshot.events)
        {
            input_system.EnqueueActionEvent(event.action_name, event.state, event.source,
                                             event.component_mask);
        }
        world.Tick(0.5f);

        auto *const camera = world.FindActor(camera_handle)->FindComponent<
            kpengine::gameplay::CameraComponent>();
        ASSERT_NE(camera, nullptr);
        EXPECT_NEAR(camera->GetLocalLocation().z_, 275.0f, 0.0001f);

        sample.axes[1] = 0.0f;
        sample.axes[2] = 0.6f;
        dispatcher.Dispatch(sample);
        world.Tick(0.5f);
        EXPECT_NEAR(camera->GetLocalTransform().rotator_.yaw_, 272.5f, 0.0001f);

        sample.axes[2] = 0.0f;
        sample.axes[5] = 0.5f;
        dispatcher.Dispatch(sample);
        world.Tick(0.5f);
        EXPECT_NEAR(camera->GetLocalLocation().y_, 25.0f, 0.0001f);

        sample.connected = false;
        sample.axes.fill(0.0f);
        dispatcher.Dispatch(sample);
        world.Tick(0.5f);
        EXPECT_NEAR(camera->GetLocalLocation().y_, 25.0f, 0.0001f);

        controller->Unpossess();
        EXPECT_FALSE(controller->GetPossessedActor().IsValid());
        EXPECT_FALSE(context->GetHandleByInputKey(
                         {kpengine::input::InputDevice::Gamepad,
                          static_cast<int>(kpengine::input::GamepadAxisCode::LeftStick)})
                         .IsValid());
    }

    input_system.Shutdown();
}
