#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>
#include <variant>
#include <vector>

#include "gameplay/component/camera_component.h"
#include "gameplay/component/directional_light_component.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/component/point_light_component.h"
#include "gameplay/component/scene_component.h"
#include "gameplay/component/spot_light_component.h"
#include "gameplay/reflection/gameplay_reflection.h"
#include "gameplay/world/gameplay_world.h"
#include "reflection/entt/entt_reflection_registry.h"
#include "reflection/reflection_system.h"
#include "render/camera_source.h"
#include "render/light/light_source.h"
#include "render/render_source.h"

namespace
{
    using namespace kpengine;
    using namespace kpengine::reflection;

    class RecordingRenderableSink final : public render::IRenderableSourceSink
    {
    public:
        render::RenderableSourceHandle EnqueueCreate(
            const render::PrimitiveRenderableSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(render::RenderableSourceHandle handle,
                           const render::PrimitiveRenderableSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(render::RenderableSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            render::RenderableSourceHandle handle;
            render::PrimitiveRenderableSourceDesc source;
        };

        uint32_t next_id = 1;
        std::vector<render::PrimitiveRenderableSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<render::RenderableSourceHandle> destroys;
    };

    class RecordingLightSink final : public render::ILightSourceSink
    {
    public:
        render::LightSourceHandle EnqueueCreate(const render::LightSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(render::LightSourceHandle handle,
                           const render::LightSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(render::LightSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            render::LightSourceHandle handle;
            render::LightSourceDesc source;
        };

        uint32_t next_id = 1;
        std::vector<render::LightSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<render::LightSourceHandle> destroys;
    };

    class RecordingCameraSink final : public render::ICameraSourceSink
    {
    public:
        render::CameraSourceHandle EnqueueCreate(const render::CameraSourceDesc &source) override
        {
            creates.push_back(source);
            return {next_id++, 0};
        }

        bool EnqueueUpdate(render::CameraSourceHandle handle,
                           const render::CameraSourceDesc &source) override
        {
            updates.push_back({handle, source});
            return true;
        }

        bool EnqueueDestroy(render::CameraSourceHandle handle) override
        {
            destroys.push_back(handle);
            return true;
        }

        struct Update
        {
            render::CameraSourceHandle handle;
            render::CameraSourceDesc source;
        };

        uint32_t next_id = 1;
        std::vector<render::CameraSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<render::CameraSourceHandle> destroys;
    };

    const ReflectionPropertyDescriptor *FindProperty(
        const ReflectionTypeDescriptor &type, std::string_view name)
    {
        for (const ReflectionPropertyDescriptor &property : type.properties)
        {
            if (property.name == name)
            {
                return &property;
            }
        }
        return nullptr;
    }

    const ReflectionTypeDescriptor &FindType(const IReflectionCatalog &catalog,
                                             std::string_view name)
    {
        return *catalog.FindType(name);
    }

    void ExpectProperties(const ReflectionTypeDescriptor &type,
                          std::initializer_list<std::string_view> expected_names)
    {
        ASSERT_EQ(type.properties.size(), expected_names.size()) << type.name;
        std::unordered_set<uint32_t> property_ids;
        for (const std::string_view name : expected_names)
        {
            const ReflectionPropertyDescriptor *const property = FindProperty(type, name);
            ASSERT_NE(property, nullptr) << type.name << ": " << name;
            if (property != nullptr)
            {
                EXPECT_TRUE(property_ids.insert(property->id.value).second)
                    << type.name << ": duplicate property ID for " << name;
            }
        }
    }
}

TEST(GameplayReflection, RegistersCanonicalFlatComponentCatalog)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({kpengine::gameplay::RegisterGameplayReflection}));
    const IReflectionCatalog &catalog = *system.GetCatalog();

    const std::vector<std::string> expected{
        "kpengine.gameplay.CameraComponent",
        "kpengine.gameplay.DirectionalLightComponent",
        "kpengine.gameplay.MeshComponent",
        "kpengine.gameplay.PointLightComponent",
        "kpengine.gameplay.SceneComponent",
        "kpengine.gameplay.SpotLightComponent",
    };
    const std::vector<ReflectionTypeDescriptor> types = catalog.EnumerateTypes();
    ASSERT_EQ(types.size(), expected.size());
    for (const std::string &name : expected)
    {
        const ReflectionTypeDescriptor *const type = catalog.FindType(name);
        ASSERT_NE(type, nullptr) << name;
        EXPECT_FALSE(type->properties.empty());
    }

    ExpectProperties(FindType(catalog, "kpengine.gameplay.SceneComponent"), {
        "transform.location.x", "transform.location.y", "transform.location.z",
        "transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll",
        "transform.scale.x", "transform.scale.y", "transform.scale.z"});
    ExpectProperties(FindType(catalog, "kpengine.gameplay.MeshComponent"), {
        "transform.location.x", "transform.location.y", "transform.location.z",
        "transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll",
        "transform.scale.x", "transform.scale.y", "transform.scale.z", "render.visible",
        "render.casts_shadow", "mesh.lod_bias"});
    ExpectProperties(FindType(catalog, "kpengine.gameplay.DirectionalLightComponent"), {
        "transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll",
        "light.color.r", "light.color.g", "light.color.b", "light.intensity",
        "light.enabled", "light.casts_shadow"});
    ExpectProperties(FindType(catalog, "kpengine.gameplay.PointLightComponent"), {
        "transform.location.x", "transform.location.y", "transform.location.z",
        "light.color.r", "light.color.g", "light.color.b", "light.intensity",
        "light.enabled", "light.casts_shadow", "light.range"});
    ExpectProperties(FindType(catalog, "kpengine.gameplay.SpotLightComponent"), {
        "transform.location.x", "transform.location.y", "transform.location.z",
        "transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll",
        "light.color.r", "light.color.g", "light.color.b", "light.intensity",
        "light.enabled", "light.casts_shadow", "light.range", "light.inner_cone",
        "light.outer_cone"});
    ExpectProperties(FindType(catalog, "kpengine.gameplay.CameraComponent"), {
        "transform.location.x", "transform.location.y", "transform.location.z",
        "transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll",
        "camera.projection", "camera.field_of_view", "camera.near_plane",
        "camera.far_plane", "camera.orthographic_height", "camera.enabled",
        "camera.priority"});

    const ReflectionTypeDescriptor &mesh = FindType(catalog, expected[2]);
    const ReflectionPropertyDescriptor *const location =
        FindProperty(mesh, "transform.location.x");
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->metadata.semantic, ReflectionWidgetSemantic::Position);
    EXPECT_EQ(location->metadata.category, "Transform");
    ASSERT_NE(FindProperty(mesh, "render.visible"), nullptr);
    ASSERT_NE(FindProperty(mesh, "render.casts_shadow"), nullptr);
    ASSERT_NE(FindProperty(mesh, "mesh.lod_bias"), nullptr);
    EXPECT_NE(FindProperty(FindType(catalog, expected[4]), "transform.scale.x"), nullptr);
    EXPECT_NE(FindProperty(FindType(catalog, expected[0]), "camera.projection"), nullptr);
    const ReflectionPropertyDescriptor *const range =
        FindProperty(FindType(catalog, "kpengine.gameplay.PointLightComponent"), "light.range");
    ASSERT_NE(range, nullptr);
    ASSERT_TRUE(range->metadata.minimum.has_value());
    EXPECT_DOUBLE_EQ(*range->metadata.minimum,
                     static_cast<double>(std::numeric_limits<float>::denorm_min()));
    const ReflectionPropertyDescriptor *const outer_cone =
        FindProperty(FindType(catalog, "kpengine.gameplay.SpotLightComponent"),
                     "light.outer_cone");
    ASSERT_NE(outer_cone, nullptr);
    ASSERT_TRUE(outer_cone->metadata.maximum.has_value());
    EXPECT_DOUBLE_EQ(*outer_cone->metadata.maximum,
                     static_cast<double>(std::nextafter(1.5707963267948966f, 0.0f)));
    system.Shutdown();
}

TEST(GameplayReflection, MeshWritesUsePublicSettersAndCoalesceSourceUpdates)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({kpengine::gameplay::RegisterGameplayReflection}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();
    const ReflectionTypeDescriptor &type =
        FindType(catalog, "kpengine.gameplay.MeshComponent");

    RecordingRenderableSink sink;
    gameplay::GameplayWorld world{&sink};
    const gameplay::ActorHandle handle = world.CreateActor();
    gameplay::Actor *const actor = world.FindActor(handle);
    ASSERT_NE(actor, nullptr);
    gameplay::MeshComponent *const mesh = actor->AddComponent<gameplay::MeshComponent>();
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(actor->SetRootComponent(mesh));
    ASSERT_TRUE(world.InitializeActor(handle));
    ASSERT_TRUE(world.ActivateActor(handle));

    const ReflectionObjectRef object = ReflectionObjectRef::ForMutable(type.id, mesh);
    ASSERT_TRUE(access.Write(object, FindProperty(type, "transform.location.x")->id,
                             ReflectionValue{3.0}));
    ASSERT_TRUE(access.Write(object, FindProperty(type, "mesh.lod_bias")->id,
                             ReflectionValue{2}));
    ASSERT_TRUE(access.Write(object, FindProperty(type, "render.visible")->id,
                             ReflectionValue{false}));
    ASSERT_TRUE(access.Write(object, FindProperty(type, "render.casts_shadow")->id,
                             ReflectionValue{false}));

    const ReflectionObjectRef const_object = ReflectionObjectRef::ForConst(type.id, mesh);
    EXPECT_TRUE(access.Read(const_object, FindProperty(type, "mesh.lod_bias")->id));
    EXPECT_EQ(access.Write(const_object, FindProperty(type, "mesh.lod_bias")->id,
                           ReflectionValue{3})
                  .status,
              ReflectionResultStatus::ReadOnly);
    ReflectionResultStatus wrong_thread_status = ReflectionResultStatus::Success;
    std::thread wrong_thread([&] {
        wrong_thread_status = access.Read(object, FindProperty(type, "mesh.lod_bias")->id).status;
    });
    wrong_thread.join();
    EXPECT_EQ(wrong_thread_status, ReflectionResultStatus::InvalidObject);

    world.Tick(0.0f);

    ASSERT_EQ(sink.updates.size(), 1U);
    const auto &updated = std::get<render::StaticMeshRenderableSourceDesc>(
        sink.updates.back().source);
    EXPECT_FLOAT_EQ(updated.world_transform.position_.x_, 3.0f);
    EXPECT_EQ(updated.lod_bias, 2);
    EXPECT_FALSE(updated.flags.visible);
    EXPECT_FALSE(updated.flags.casts_shadow);
    system.Shutdown();
}

TEST(GameplayReflection, TransformWritesPropagateToAttachedMeshSources)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({kpengine::gameplay::RegisterGameplayReflection}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();
    const ReflectionTypeDescriptor &scene_type =
        FindType(catalog, "kpengine.gameplay.SceneComponent");
    const ReflectionTypeDescriptor &mesh_type =
        FindType(catalog, "kpengine.gameplay.MeshComponent");

    RecordingRenderableSink sink;
    gameplay::GameplayWorld world{&sink};
    const gameplay::ActorHandle parent_handle = world.CreateActor();
    const gameplay::ActorHandle child_handle = world.CreateActor();
    gameplay::Actor *const parent_actor = world.FindActor(parent_handle);
    gameplay::Actor *const child_actor = world.FindActor(child_handle);
    ASSERT_NE(parent_actor, nullptr);
    ASSERT_NE(child_actor, nullptr);

    gameplay::SceneComponent *const parent =
        parent_actor->AddComponent<gameplay::SceneComponent>();
    gameplay::MeshComponent *const child = child_actor->AddComponent<gameplay::MeshComponent>();
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(parent_actor->SetRootComponent(parent));
    ASSERT_TRUE(child_actor->SetRootComponent(child));
    parent->SetLocalLocation({10.0f, 0.0f, 0.0f});
    child->SetLocalLocation({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(child->AttachTo(*parent));
    ASSERT_TRUE(world.InitializeActor(parent_handle));
    ASSERT_TRUE(world.InitializeActor(child_handle));
    ASSERT_TRUE(world.ActivateActor(parent_handle));
    ASSERT_TRUE(world.ActivateActor(child_handle));
    sink.updates.clear();

    const ReflectionObjectRef parent_object = ReflectionObjectRef::ForMutable(scene_type.id, parent);
    const ReflectionObjectRef child_object = ReflectionObjectRef::ForMutable(mesh_type.id, child);
    ASSERT_TRUE(access.Write(parent_object, FindProperty(scene_type, "transform.location.x")->id,
                             ReflectionValue{20.0}));
    ASSERT_TRUE(access.Write(child_object, FindProperty(mesh_type, "transform.location.x")->id,
                             ReflectionValue{3.0}));
    world.Tick(0.0f);

    ASSERT_EQ(sink.updates.size(), 1U);
    const auto &updated = std::get<render::StaticMeshRenderableSourceDesc>(
        sink.updates.front().source);
    EXPECT_FLOAT_EQ(updated.world_transform.position_.x_, 23.0f);
    system.Shutdown();
}

TEST(GameplayReflection, LightAndCameraWritesRejectInvalidValuesWithoutSourceUpdates)
{
    ReflectionSystem system;
    ASSERT_TRUE(system.Initialize({kpengine::gameplay::RegisterGameplayReflection}));
    const IReflectionCatalog &catalog = *system.GetCatalog();
    const IReflectionAccess &access = *system.GetAccess();

    RecordingLightSink light_sink;
    gameplay::GameplayWorld light_world{nullptr, &light_sink};
    const gameplay::ActorHandle light_handle = light_world.CreateActor();
    gameplay::Actor *const light_actor = light_world.FindActor(light_handle);
    ASSERT_NE(light_actor, nullptr);
    gameplay::SceneComponent *const point_parent =
        light_actor->AddComponent<gameplay::SceneComponent>();
    gameplay::PointLightComponent *const point =
        light_actor->AddComponent<gameplay::PointLightComponent>();
    ASSERT_NE(point_parent, nullptr);
    ASSERT_NE(point, nullptr);
    ASSERT_TRUE(light_actor->SetRootComponent(point_parent));
    point_parent->SetLocalLocation({10.0f, 0.0f, 0.0f});
    point->SetLocalLocation({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(point->AttachTo(*point_parent));
    ASSERT_TRUE(light_world.InitializeActor(light_handle));
    ASSERT_TRUE(light_world.ActivateActor(light_handle));
    const ReflectionTypeDescriptor &point_type =
        FindType(catalog, "kpengine.gameplay.PointLightComponent");
    const ReflectionObjectRef point_object = ReflectionObjectRef::ForMutable(point_type.id, point);
    EXPECT_EQ(access.Write(point_object, FindProperty(point_type, "light.range")->id,
                           ReflectionValue{-1.0})
                  .status,
              ReflectionResultStatus::SetterRejected);
    light_world.Tick(0.0f);
    EXPECT_TRUE(light_sink.updates.empty());
    ASSERT_TRUE(access.Write(point_object, FindProperty(point_type, "transform.location.x")->id,
                             ReflectionValue{3.0}));
    ASSERT_TRUE(access.Write(point_object, FindProperty(point_type, "light.range")->id,
                             ReflectionValue{4.0}));
    light_world.Tick(0.0f);
    ASSERT_EQ(light_sink.updates.size(), 1U);
    const auto &point_update =
        std::get<render::PointLightSourceDesc>(light_sink.updates.back().source);
    EXPECT_FLOAT_EQ(point_update.position.x_, 13.0f);
    EXPECT_FLOAT_EQ(point_update.range, 4.0f);

    RecordingLightSink directional_sink;
    gameplay::GameplayWorld directional_world{nullptr, &directional_sink};
    const gameplay::ActorHandle directional_handle = directional_world.CreateActor();
    gameplay::Actor *const directional_actor = directional_world.FindActor(directional_handle);
    ASSERT_NE(directional_actor, nullptr);
    gameplay::DirectionalLightComponent *const directional =
        directional_actor->AddComponent<gameplay::DirectionalLightComponent>();
    ASSERT_NE(directional, nullptr);
    ASSERT_TRUE(directional_actor->SetRootComponent(directional));
    ASSERT_TRUE(directional_world.InitializeActor(directional_handle));
    ASSERT_TRUE(directional_world.ActivateActor(directional_handle));
    const ReflectionTypeDescriptor &directional_type =
        FindType(catalog, "kpengine.gameplay.DirectionalLightComponent");
    const ReflectionObjectRef directional_object =
        ReflectionObjectRef::ForMutable(directional_type.id, directional);
    ASSERT_TRUE(access.Write(directional_object,
                             FindProperty(directional_type, "transform.rotation.yaw")->id,
                             ReflectionValue{-90.0}));
    ASSERT_TRUE(access.Write(directional_object,
                             FindProperty(directional_type, "light.color.r")->id,
                             ReflectionValue{0.25}));
    ASSERT_TRUE(access.Write(directional_object,
                             FindProperty(directional_type, "light.intensity")->id,
                             ReflectionValue{3.0}));
    ASSERT_TRUE(access.Write(directional_object,
                             FindProperty(directional_type, "light.enabled")->id,
                             ReflectionValue{false}));
    ASSERT_TRUE(access.Write(directional_object,
                             FindProperty(directional_type, "light.casts_shadow")->id,
                             ReflectionValue{false}));
    directional_world.Tick(0.0f);
    ASSERT_EQ(directional_sink.updates.size(), 1U);
    const auto &directional_update =
        std::get<render::DirectionalLightSourceDesc>(directional_sink.updates.back().source);
    EXPECT_NEAR(directional_update.direction.z_, -1.0f, 0.0001f);
    EXPECT_FLOAT_EQ(directional_update.color.x_, 0.25f);
    EXPECT_FLOAT_EQ(directional_update.intensity, 3.0f);
    EXPECT_FALSE(directional_update.enabled);
    EXPECT_FALSE(directional_update.casts_shadow);

    RecordingLightSink spot_sink;
    gameplay::GameplayWorld spot_world{nullptr, &spot_sink};
    const gameplay::ActorHandle spot_handle = spot_world.CreateActor();
    gameplay::Actor *const spot_actor = spot_world.FindActor(spot_handle);
    ASSERT_NE(spot_actor, nullptr);
    gameplay::SceneComponent *const spot_parent =
        spot_actor->AddComponent<gameplay::SceneComponent>();
    gameplay::SpotLightComponent *const spot =
        spot_actor->AddComponent<gameplay::SpotLightComponent>();
    ASSERT_NE(spot_parent, nullptr);
    ASSERT_NE(spot, nullptr);
    ASSERT_TRUE(spot_actor->SetRootComponent(spot_parent));
    spot_parent->SetLocalLocation({10.0f, 0.0f, 0.0f});
    spot->SetLocalLocation({2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(spot->AttachTo(*spot_parent));
    ASSERT_TRUE(spot_world.InitializeActor(spot_handle));
    ASSERT_TRUE(spot_world.ActivateActor(spot_handle));
    const ReflectionTypeDescriptor &spot_type =
        FindType(catalog, "kpengine.gameplay.SpotLightComponent");
    const ReflectionObjectRef spot_object = ReflectionObjectRef::ForMutable(spot_type.id, spot);
    ASSERT_TRUE(access.Write(spot_object, FindProperty(spot_type, "transform.rotation.yaw")->id,
                             ReflectionValue{-90.0}));
    ASSERT_TRUE(access.Write(spot_object, FindProperty(spot_type, "light.inner_cone")->id,
                             ReflectionValue{0.2}));
    ASSERT_TRUE(access.Write(spot_object, FindProperty(spot_type, "light.outer_cone")->id,
                             ReflectionValue{0.9}));
    spot_world.Tick(0.0f);
    ASSERT_EQ(spot_sink.updates.size(), 1U);
    const auto &spot_update = std::get<render::SpotLightSourceDesc>(spot_sink.updates.back().source);
    EXPECT_FLOAT_EQ(spot_update.position.x_, 12.0f);
    EXPECT_NEAR(spot_update.direction.z_, -1.0f, 0.0001f);
    EXPECT_FLOAT_EQ(spot_update.inner_cone_radians, 0.2f);
    EXPECT_FLOAT_EQ(spot_update.outer_cone_radians, 0.9f);
    spot_sink.updates.clear();
    EXPECT_EQ(access.Write(spot_object, FindProperty(spot_type, "light.inner_cone")->id,
                           ReflectionValue{1.0})
                  .status,
              ReflectionResultStatus::SetterRejected);
    EXPECT_EQ(access.Write(spot_object, FindProperty(spot_type, "light.outer_cone")->id,
                           ReflectionValue{1.5707963267948966})
                  .status,
              ReflectionResultStatus::SetterRejected);
    spot_world.Tick(0.0f);
    EXPECT_TRUE(spot_sink.updates.empty());

    RecordingCameraSink camera_sink;
    gameplay::GameplayWorld camera_world{nullptr, nullptr, &camera_sink};
    const gameplay::ActorHandle camera_handle = camera_world.CreateActor();
    gameplay::Actor *const camera_actor = camera_world.FindActor(camera_handle);
    ASSERT_NE(camera_actor, nullptr);
    gameplay::CameraComponent *const camera =
        camera_actor->AddComponent<gameplay::CameraComponent>();
    ASSERT_NE(camera, nullptr);
    ASSERT_TRUE(camera_actor->SetRootComponent(camera));
    ASSERT_TRUE(camera_world.InitializeActor(camera_handle));
    ASSERT_TRUE(camera_world.ActivateActor(camera_handle));
    const ReflectionTypeDescriptor &camera_type =
        FindType(catalog, "kpengine.gameplay.CameraComponent");
    const ReflectionObjectRef camera_object = ReflectionObjectRef::ForMutable(camera_type.id, camera);
    EXPECT_EQ(access.Write(camera_object, FindProperty(camera_type, "camera.field_of_view")->id,
                           ReflectionValue{0.0})
                  .status,
              ReflectionResultStatus::SetterRejected);
    EXPECT_EQ(access.Write(camera_object, FindProperty(camera_type, "camera.projection")->id,
                           ReflectionValue{2})
                  .status,
              ReflectionResultStatus::SetterRejected);
    EXPECT_EQ(access.Write(camera_object, FindProperty(camera_type, "camera.near_plane")->id,
                           ReflectionValue{2000.0})
                  .status,
              ReflectionResultStatus::SetterRejected);
    EXPECT_EQ(access.Write(camera_object, FindProperty(camera_type, "camera.far_plane")->id,
                           ReflectionValue{0.05})
                  .status,
              ReflectionResultStatus::SetterRejected);
    camera_world.Tick(0.0f);
    EXPECT_TRUE(camera_sink.updates.empty());
    ASSERT_TRUE(access.Write(camera_object, FindProperty(camera_type, "camera.field_of_view")->id,
                             ReflectionValue{60.0}));
    ASSERT_TRUE(access.Write(camera_object, FindProperty(camera_type, "camera.projection")->id,
                             ReflectionValue{1}));
    ASSERT_TRUE(access.Write(camera_object, FindProperty(camera_type, "camera.near_plane")->id,
                             ReflectionValue{1.0}));
    ASSERT_TRUE(access.Write(camera_object, FindProperty(camera_type, "camera.far_plane")->id,
                             ReflectionValue{500.0}));
    camera_world.Tick(0.0f);
    ASSERT_EQ(camera_sink.updates.size(), 1U);
    EXPECT_FLOAT_EQ(camera_sink.updates.back().source.field_of_view_degrees, 60.0f);
    EXPECT_EQ(camera_sink.updates.back().source.projection_mode,
              render::CameraProjectionMode::Orthographic);
    EXPECT_FLOAT_EQ(camera_sink.updates.back().source.near_plane, 1.0f);
    EXPECT_FLOAT_EQ(camera_sink.updates.back().source.far_plane, 500.0f);
    system.Shutdown();
}
