#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_manager.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/texture.h"
#include "data/mesh.h"
#include "gameplay/actor/actor.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "level/level_instance.h"
#include "render/camera_source.h"
#include "render/environment_source.h"
#include "render/environment_source_registry.h"
#include "render/light/light_source.h"
#include "render/render_source.h"

namespace
{
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetManager;
    using kpengine::asset::AssetRegisterInfo;
    using kpengine::asset::AssetType;
    using kpengine::asset::LevelObject;
    using kpengine::asset::LevelResource;
    using kpengine::asset::LevelStaticMeshRecord;
    using kpengine::asset::MaterialResource;
    using kpengine::asset::MeshResource;
    using kpengine::asset::ModelGeometryType;
    using kpengine::asset::ModelResource;

    struct EventLog
    {
        std::vector<std::string> events;
    };

    class RecordingSourceSink final : public kpengine::render::IRenderableSourceSink
    {
    public:
        explicit RecordingSourceSink(EventLog *event_log = nullptr) : event_log(event_log) {}

        kpengine::render::RenderableSourceHandle EnqueueCreate(
            const kpengine::render::PrimitiveRenderableSourceDesc &source) override
        {
            creates.push_back(source);
            if (event_log != nullptr)
            {
                event_log->events.push_back("renderable_create");
            }
            return {next_handle++, 0};
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
            if (event_log != nullptr)
            {
                event_log->events.push_back("renderable_destroy");
            }
            return true;
        }

        struct Update
        {
            kpengine::render::RenderableSourceHandle handle;
            kpengine::render::PrimitiveRenderableSourceDesc source;
        };

        EventLog *event_log = nullptr;
        uint32_t next_handle = 0;
        std::vector<kpengine::render::PrimitiveRenderableSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<kpengine::render::RenderableSourceHandle> destroys;
    };

    class RecordingLightSink final : public kpengine::render::ILightSourceSink
    {
    public:
        explicit RecordingLightSink(EventLog *event_log = nullptr) : event_log(event_log) {}

        kpengine::render::LightSourceHandle EnqueueCreate(
            const kpengine::render::LightSourceDesc &source) override
        {
            creates.push_back(source);
            if (event_log != nullptr)
            {
                event_log->events.push_back("light_create");
            }
            return {next_handle++, 0};
        }

        bool EnqueueUpdate(kpengine::render::LightSourceHandle handle,
                           const kpengine::render::LightSourceDesc &source) override
        {
            (void)handle;
            updates.push_back(source);
            return true;
        }

        bool EnqueueDestroy(kpengine::render::LightSourceHandle handle) override
        {
            destroys.push_back(handle);
            if (event_log != nullptr)
            {
                event_log->events.push_back("light_destroy");
            }
            return true;
        }

        EventLog *event_log = nullptr;
        uint32_t next_handle = 0;
        std::vector<kpengine::render::LightSourceDesc> creates;
        std::vector<kpengine::render::LightSourceDesc> updates;
        std::vector<kpengine::render::LightSourceHandle> destroys;
    };

    class RecordingCameraSink final : public kpengine::render::ICameraSourceSink
    {
    public:
        explicit RecordingCameraSink(EventLog *event_log = nullptr) : event_log(event_log) {}

        kpengine::render::CameraSourceHandle EnqueueCreate(
            const kpengine::render::CameraSourceDesc &source) override
        {
            creates.push_back(source);
            if (event_log != nullptr)
            {
                event_log->events.push_back("camera_create");
            }
            return {next_handle++, 0};
        }

        bool EnqueueUpdate(kpengine::render::CameraSourceHandle handle,
                           const kpengine::render::CameraSourceDesc &source) override
        {
            (void)handle;
            updates.push_back(source);
            return true;
        }

        bool EnqueueDestroy(kpengine::render::CameraSourceHandle handle) override
        {
            destroys.push_back(handle);
            if (event_log != nullptr)
            {
                event_log->events.push_back("camera_destroy");
            }
            return true;
        }

        EventLog *event_log = nullptr;
        uint32_t next_handle = 0;
        std::vector<kpengine::render::CameraSourceDesc> creates;
        std::vector<kpengine::render::CameraSourceDesc> updates;
        std::vector<kpengine::render::CameraSourceHandle> destroys;
    };

    class RecordingEnvironmentSink final : public kpengine::render::IEnvironmentSourceSink
    {
    public:
        explicit RecordingEnvironmentSink(EventLog *event_log = nullptr) : event_log(event_log) {}

        kpengine::render::EnvironmentSourceHandle EnqueueCreate(
            const kpengine::render::EnvironmentSourceDesc &source) override
        {
            if (reject_creates)
            {
                return {};
            }
            creates.push_back(source);
            if (event_log != nullptr)
            {
                event_log->events.push_back("environment_create");
            }
            return {next_handle++, 0};
        }

        bool EnqueueDestroy(kpengine::render::EnvironmentSourceHandle handle) override
        {
            destroys.push_back(handle);
            if (event_log != nullptr)
            {
                event_log->events.push_back("environment_destroy");
            }
            return true;
        }

        EventLog *event_log = nullptr;
        bool reject_creates = false;
        uint32_t next_handle = 0;
        std::vector<kpengine::render::EnvironmentSourceDesc> creates;
        std::vector<kpengine::render::EnvironmentSourceHandle> destroys;
    };

    class AssetFixture
    {
    public:
        AssetFixture()
        {
            auto mesh_data = std::make_shared<kpengine::data::MeshData>();
            mesh_data->vertices.resize(3);
            mesh_data->indices = {0, 1, 2};
            mesh_data->sections.push_back({0, 3, 0});

            auto mesh = std::make_shared<MeshResource>();
            mesh->data = std::move(mesh_data);
            mesh->local_bounds = {{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}};

            AssetRegisterInfo mesh_info{};
            mesh_info.resource = mesh;
            mesh_info.path = "runtime_level_test.mesh";
            mesh_info.name = "RuntimeLevelTestMesh";
            mesh_info.type = AssetType::KPAT_Mesh;
            mesh_id = assets.RegisterAsset(mesh_info);

            auto model = std::make_shared<ModelResource>();
            model->BindData(ModelGeometryType::KPMG_Mesh, mesh_id);
            AssetRegisterInfo model_info{};
            model_info.resource = model;
            model_info.path = "runtime_level_test.model";
            model_info.name = "RuntimeLevelTestModel";
            model_info.dependencies = {mesh_id};
            model_info.type = AssetType::KPAT_Model;
            model_id = assets.RegisterAsset(model_info);

            auto material = std::make_shared<MaterialResource>();
            AssetRegisterInfo material_info{};
            material_info.resource = material;
            material_info.path = "runtime_level_test.material";
            material_info.name = "RuntimeLevelTestMaterial";
            material_info.type = AssetType::KPAT_Material;
            material_id = assets.RegisterAsset(material_info);

            auto texture_data = std::make_shared<kpengine::data::TextureData>();
            texture_data->width = 4;
            texture_data->height = 2;
            texture_data->format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
            texture_data->pixels.resize(4 * 2 * 4 * sizeof(uint16_t), 0);
            auto texture = std::make_shared<kpengine::asset::TextureResource>();
            texture->data = std::move(texture_data);
            AssetRegisterInfo texture_info{};
            texture_info.resource = texture;
            texture_info.path = "runtime_level_test.environment.texture";
            texture_info.name = "RuntimeLevelTestEnvironment";
            texture_info.type = AssetType::KPAT_Texture;
            environment_texture_id = assets.RegisterAsset(texture_info);
        }

        ~AssetFixture()
        {
            for (const AssetID id : level_ids)
            {
                assets.UnRegisterAsset(id);
            }
            for (auto it = extra_asset_ids.rbegin(); it != extra_asset_ids.rend(); ++it)
            {
                assets.UnRegisterAsset(*it);
            }
            assets.UnRegisterAsset(model_id);
            assets.UnRegisterAsset(material_id);
            assets.UnRegisterAsset(mesh_id);
            assets.UnRegisterAsset(environment_texture_id);
        }

        AssetID AddLevel(std::vector<LevelObject> objects,
                         std::vector<AssetID> dependencies = {},
                         std::optional<kpengine::asset::LevelEnvironmentRecord> environment =
                             std::nullopt)
        {
            auto level = std::make_shared<LevelResource>();
            level->objects = std::move(objects);
            level->environment = environment;

            if (dependencies.empty())
            {
                dependencies = {model_id, material_id};
                if (environment.has_value())
                {
                    dependencies.push_back(environment_texture_id);
                }
            }

            AssetRegisterInfo info{};
            info.resource = level;
            info.path = "runtime_level_test_" + std::to_string(level_ids.size()) + ".level";
            info.name = "RuntimeLevelTestLevel";
            info.dependencies = std::move(dependencies);
            info.type = AssetType::KPAT_Level;
            const AssetID id = assets.RegisterAsset(info);
            level_ids.push_back(id);
            return id;
        }

        AssetID AddExtraAsset(AssetRegisterInfo info)
        {
            const AssetID id = assets.RegisterAsset(info);
            extra_asset_ids.push_back(id);
            return id;
        }

        AssetManager &assets = AssetManager::GetInstance();
        AssetID mesh_id;
        AssetID model_id;
        AssetID material_id;
        AssetID environment_texture_id;
        std::vector<AssetID> level_ids;
        std::vector<AssetID> extra_asset_ids;
    };

    LevelStaticMeshRecord MakeMeshRecord(const char *id, int lod_bias = 0)
    {
        LevelStaticMeshRecord record{};
        record.id = id;
        record.name = id;
        record.transform.position = {10.0f, 20.0f, 30.0f};
        record.transform.rotation_degrees = {1.0f, 2.0f, 3.0f};
        record.transform.scale = {2.0f, 3.0f, 4.0f};
        record.model.dependency_index = 0;
        record.material.dependency_index = 1;
        record.visible = false;
        record.casts_shadow = true;
        record.lod_bias = lod_bias;
        return record;
    }
}

TEST(RuntimeLevelTest, InstantiatesStaticMeshesInAuthoredOrderAndMapsIDs)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("first", 2), MakeMeshRecord("second", -1)});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const kpengine::runtime::LevelInstanceResult result = instance.Instantiate(level_id);
    ASSERT_TRUE(result) << result.diagnostic;
    EXPECT_TRUE(instance.IsActive());
    EXPECT_EQ(instance.GetLevelAsset(), level_id);
    EXPECT_EQ(instance.GetActorCount(), 2U);
    ASSERT_EQ(source_sink.creates.size(), 2U);

    const auto &first = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(
        source_sink.creates[0]);
    const auto &second = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(
        source_sink.creates[1]);
    EXPECT_EQ(first.mesh_asset, assets.mesh_id);
    EXPECT_EQ(first.material_asset, assets.material_id);
    EXPECT_EQ(first.world_transform.position_, (kpengine::Vector3f{10.0f, 20.0f, 30.0f}));
    EXPECT_EQ(first.world_transform.rotator_, (kpengine::Rotatorf{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(first.world_transform.scale_, (kpengine::Vector3f{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(first.local_bounds,
              (kpengine::spatial::AABB{{-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}}));
    EXPECT_FALSE(first.flags.visible);
    EXPECT_TRUE(first.flags.casts_shadow);
    EXPECT_EQ(first.lod_bias, 2);
    EXPECT_EQ(second.lod_bias, -1);

    const auto first_actor = instance.FindActor("first");
    const auto second_actor = instance.FindActor("second");
    ASSERT_TRUE(first_actor.has_value());
    ASSERT_TRUE(second_actor.has_value());
    EXPECT_EQ(world.FindActor(*first_actor)->GetState(), kpengine::gameplay::ActorState::Active);
    EXPECT_EQ(world.FindActor(*second_actor)->GetState(), kpengine::gameplay::ActorState::Active);
    EXPECT_FALSE(instance.FindActor("unknown").has_value());

    instance.Unload();
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_EQ(source_sink.destroys.size(), 2U);
    EXPECT_EQ(source_sink.destroys[0], (kpengine::render::RenderableSourceHandle{1, 0}));
    EXPECT_EQ(source_sink.destroys[1], (kpengine::render::RenderableSourceHandle{0, 0}));
    instance.Unload();
    EXPECT_EQ(source_sink.destroys.size(), 2U);
}

TEST(RuntimeLevelTest, InstantiatesNonMeshRecordsAndAllowsEmptyInstance)
{
    AssetFixture assets;
    RecordingLightSink light_sink;
    kpengine::gameplay::GameplayWorld world{nullptr, &light_sink};
    kpengine::asset::LevelDirectionalLightRecord light{};
    light.id = "light";
    const AssetID level_id = assets.AddLevel({light});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_TRUE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 1U);
    EXPECT_EQ(light_sink.creates.size(), 1U);
    instance.Unload();
    EXPECT_EQ(light_sink.destroys.size(), 1U);
}

TEST(RuntimeLevelTest, RejectsInvalidPreflightBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    LevelStaticMeshRecord record = MakeMeshRecord("bad");
    record.model.dependency_index = 9;
    const AssetID level_id = assets.AddLevel({record});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::DependencyResolutionFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsInvalidLevelAssetBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    kpengine::runtime::LevelInstance instance{assets.assets, world};

    const auto result = instance.Instantiate(assets.mesh_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidLevelAsset);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsInvalidModelResourceBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};

    AssetRegisterInfo invalid_model_info{};
    invalid_model_info.resource = std::make_shared<MaterialResource>();
    invalid_model_info.path = "runtime_level_invalid_model_resource.model";
    invalid_model_info.name = "RuntimeLevelInvalidModelResource";
    invalid_model_info.type = AssetType::KPAT_Model;
    const AssetID invalid_model = assets.AddExtraAsset(std::move(invalid_model_info));
    const AssetID level_id =
        assets.AddLevel({MakeMeshRecord("invalid-model")}, {invalid_model, assets.material_id});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidModelResource);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsMissingMeshGeometryBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};

    auto model = std::make_shared<ModelResource>();
    AssetRegisterInfo model_info{};
    model_info.resource = std::move(model);
    model_info.path = "runtime_level_missing_geometry.model";
    model_info.name = "RuntimeLevelMissingGeometry";
    model_info.type = AssetType::KPAT_Model;
    const AssetID missing_geometry_model = assets.AddExtraAsset(std::move(model_info));
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("missing-geometry")},
                                             {missing_geometry_model, assets.material_id});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::MissingMeshGeometry);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsInvalidMeshAssetBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};

    auto model = std::make_shared<ModelResource>();
    model->BindData(ModelGeometryType::KPMG_Mesh, assets.material_id);
    AssetRegisterInfo model_info{};
    model_info.resource = std::move(model);
    model_info.path = "runtime_level_invalid_mesh_asset.model";
    model_info.name = "RuntimeLevelInvalidMeshAsset";
    model_info.type = AssetType::KPAT_Model;
    const AssetID invalid_mesh_model = assets.AddExtraAsset(std::move(model_info));
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("invalid-mesh")},
                                             {invalid_mesh_model, assets.material_id});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidMeshAsset);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsInvalidBoundsBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    auto mesh = assets.assets.GetAsset(assets.mesh_id)->GetResource<MeshResource>();
    ASSERT_NE(mesh, nullptr);
    mesh->local_bounds = {{1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f}};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("invalid-bounds")});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidMeshData);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RejectsInvalidMaterialResourceBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};

    AssetRegisterInfo invalid_material_info{};
    invalid_material_info.resource = std::make_shared<ModelResource>();
    invalid_material_info.path = "runtime_level_invalid_material_resource.material";
    invalid_material_info.name = "RuntimeLevelInvalidMaterialResource";
    invalid_material_info.type = AssetType::KPAT_Material;
    const AssetID invalid_material = assets.AddExtraAsset(std::move(invalid_material_info));
    const AssetID level_id =
        assets.AddLevel({MakeMeshRecord("invalid-material")}, {assets.model_id, invalid_material});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidMaterialResource);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.creates.empty());
    EXPECT_TRUE(source_sink.destroys.empty());
}

TEST(RuntimeLevelTest, RollsBackReverseOrderAndRetriesImmediatelyAfterCreationFailure)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("first"), MakeMeshRecord("second")});

    int factory_calls = 0;
    bool fail_second_call = true;
    kpengine::runtime::StaticMeshActorFactory factory =
        [&factory_calls, &fail_second_call](kpengine::gameplay::GameplayWorld &gameplay_world,
                                             const kpengine::gameplay::StaticMeshActorDesc &desc)
    {
        ++factory_calls;
        if (fail_second_call && factory_calls == 2)
        {
            fail_second_call = false;
            return kpengine::gameplay::ActorHandle{};
        }
        return kpengine::gameplay::CreateStaticMeshActor(gameplay_world, desc);
    };

    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factory)};
    const auto failed = instance.Instantiate(level_id);
    EXPECT_EQ(failed.error, kpengine::runtime::LevelInstanceError::ActorCreationFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    ASSERT_EQ(source_sink.creates.size(), 1U);
    ASSERT_EQ(source_sink.destroys.size(), 1U);

    const auto retry = instance.Instantiate(level_id);
    ASSERT_TRUE(retry) << retry.diagnostic;
    EXPECT_TRUE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 2U);
    EXPECT_EQ(factory_calls, 4);
    instance.Unload();
    EXPECT_EQ(source_sink.creates.size(), 3U);
    EXPECT_EQ(source_sink.destroys.size(), 3U);
}

TEST(RuntimeLevelTest, RollsBackWhenActorFactoryThrowsAfterCreatingAnActor)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("first"), MakeMeshRecord("second")});

    int factory_calls = 0;
    bool throw_on_second_call = true;
    kpengine::gameplay::ActorHandle first_handle;
    kpengine::runtime::StaticMeshActorFactory factory =
        [&factory_calls, &throw_on_second_call, &first_handle](
            kpengine::gameplay::GameplayWorld &gameplay_world,
            const kpengine::gameplay::StaticMeshActorDesc &desc)
    {
        ++factory_calls;
        if (throw_on_second_call && factory_calls == 2)
        {
            throw_on_second_call = false;
            throw std::runtime_error("injected actor factory failure");
        }
        const auto handle = kpengine::gameplay::CreateStaticMeshActor(gameplay_world, desc);
        if (factory_calls == 1)
        {
            first_handle = handle;
        }
        return handle;
    };

    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factory)};
    EXPECT_THROW(instance.Instantiate(level_id), std::runtime_error);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    ASSERT_EQ(source_sink.creates.size(), 1U);
    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(world.FindActor(first_handle), nullptr);

    const auto retry = instance.Instantiate(level_id);
    ASSERT_TRUE(retry) << retry.diagnostic;
    EXPECT_EQ(instance.GetActorCount(), 2U);
    instance.Unload();
}

TEST(RuntimeLevelTest, ActiveInstanceRejectsReplacementAndStaleLookup)
{
    AssetFixture assets;
    kpengine::gameplay::GameplayWorld world{};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("only")});
    kpengine::runtime::LevelInstance instance{assets.assets, world};
    ASSERT_TRUE(instance.Instantiate(level_id));
    const auto handle = instance.FindActor("only");
    ASSERT_TRUE(handle.has_value());

    const auto replacement = instance.Instantiate(level_id);
    EXPECT_EQ(replacement.error, kpengine::runtime::LevelInstanceError::InvalidState);
    EXPECT_EQ(instance.GetLevelAsset(), level_id);
    EXPECT_EQ(instance.GetActorCount(), 1U);

    ASSERT_TRUE(world.DestroyActor(*handle));
    world.ReclaimDestroyedActors();
    EXPECT_FALSE(instance.FindActor("only").has_value());
    instance.Unload();
}

TEST(RuntimeLevelTest, DoesNotUnregisterAssetsOnUnload)
{
    AssetFixture assets;
    kpengine::gameplay::GameplayWorld world{};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("only")});
    kpengine::runtime::LevelInstance instance{assets.assets, world};
    ASSERT_TRUE(instance.Instantiate(level_id));
    instance.Unload();

    EXPECT_NE(assets.assets.GetAsset(level_id), nullptr);
    EXPECT_NE(assets.assets.GetAsset(assets.model_id), nullptr);
    EXPECT_NE(assets.assets.GetAsset(assets.mesh_id), nullptr);
    EXPECT_NE(assets.assets.GetAsset(assets.material_id), nullptr);
}

TEST(RuntimeLevelTest, FullLifecycleReleasesDependenciesOnlyAfterLevelOwnershipEnds)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("only")});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    ASSERT_TRUE(instance.Instantiate(level_id));
    ASSERT_TRUE(instance.IsActive());
    ASSERT_EQ(instance.GetActorCount(), 1U);

    // Runtime ownership does not unregister the level or its Asset edges. A
    // direct dependency release must remain blocked while the level is live.
    assets.assets.UnRegisterAsset(assets.model_id);
    assets.assets.UnRegisterAsset(assets.material_id);
    EXPECT_NE(assets.assets.GetAsset(assets.model_id), nullptr);
    EXPECT_NE(assets.assets.GetAsset(assets.material_id), nullptr);

    instance.Unload();
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_EQ(source_sink.destroys.size(), 1U);

    // Unload retires Runtime/Gameplay ownership but deliberately leaves the
    // Asset graph intact. The parent level edge is removed explicitly by the
    // caller, after which its dependencies become unregisterable in order.
    EXPECT_NE(assets.assets.GetAsset(level_id), nullptr);
    assets.assets.UnRegisterAsset(level_id);
    EXPECT_EQ(assets.assets.GetAsset(level_id), nullptr);

    assets.assets.UnRegisterAsset(assets.model_id);
    assets.assets.UnRegisterAsset(assets.material_id);
    EXPECT_EQ(assets.assets.GetAsset(assets.model_id), nullptr);
    EXPECT_EQ(assets.assets.GetAsset(assets.material_id), nullptr);

    assets.assets.UnRegisterAsset(assets.mesh_id);
    EXPECT_EQ(assets.assets.GetAsset(assets.mesh_id), nullptr);
}

TEST(RuntimeLevelTest, DestructorUnloadsActorsAndRetiresSources)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    kpengine::gameplay::ActorHandle created_handle;

    {
        const AssetID level_id = assets.AddLevel({MakeMeshRecord("only")});
        kpengine::runtime::LevelInstance instance{assets.assets, world};
        ASSERT_TRUE(instance.Instantiate(level_id));
        const auto handle = instance.FindActor("only");
        ASSERT_TRUE(handle.has_value());
        created_handle = *handle;
    }

    EXPECT_EQ(world.FindActor(created_handle), nullptr);
    ASSERT_EQ(source_sink.creates.size(), 1U);
    ASSERT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(source_sink.destroys[0], (kpengine::render::RenderableSourceHandle{0, 0}));
}

TEST(RuntimeLevelTest, InstantiatesEveryV1ActorKindAndEnvironmentInAuthoredOrder)
{
    AssetFixture assets;
    EventLog event_log;
    RecordingSourceSink source_sink{&event_log};
    RecordingLightSink light_sink{&event_log};
    RecordingCameraSink camera_sink{&event_log};
    RecordingEnvironmentSink environment_sink{&event_log};
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink, &camera_sink};

    kpengine::asset::LevelDirectionalLightRecord directional{};
    directional.id = "directional";
    directional.direction = {1.0f, 2.0f, 3.0f};
    directional.color = {0.1f, 0.2f, 0.3f};
    directional.intensity = 4.0f;
    directional.enabled = false;
    directional.casts_shadow = false;

    kpengine::asset::LevelPointLightRecord point{};
    point.id = "point";
    point.position = {4.0f, 5.0f, 6.0f};
    point.color = {0.4f, 0.5f, 0.6f};
    point.intensity = 7.0f;
    point.range = 8.0f;
    point.enabled = false;
    point.casts_shadow = false;

    kpengine::asset::LevelSpotLightRecord spot{};
    spot.id = "spot";
    spot.position = {7.0f, 8.0f, 9.0f};
    spot.direction = {-1.0f, -2.0f, -3.0f};
    spot.color = {0.7f, 0.8f, 0.9f};
    spot.intensity = 10.0f;
    spot.range = 11.0f;
    spot.inner_cone_radians = 0.2f;
    spot.outer_cone_radians = 0.6f;
    spot.enabled = false;
    spot.casts_shadow = false;

    kpengine::asset::LevelCameraRecord camera{};
    camera.id = "camera";
    camera.transform.position = {12.0f, 13.0f, 14.0f};
    camera.transform.rotation_degrees = {15.0f, 16.0f, 17.0f};
    camera.transform.scale = {1.5f, 2.5f, 3.5f};
    camera.projection = kpengine::asset::LevelProjection::Orthographic;
    camera.near_plane = 0.2f;
    camera.far_plane = 100.0f;
    camera.field_of_view_degrees = 75.0f;
    camera.orthographic_height = 25.0f;
    camera.enabled = true;
    camera.priority = 9;

    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    environment.ibl_intensity = 1.75f;
    const AssetID level_id = assets.AddLevel(
        {MakeMeshRecord("mesh"), directional, point, spot, camera},
        {assets.model_id, assets.material_id, assets.environment_texture_id}, environment);

    kpengine::runtime::LevelInstance instance{assets.assets, world, {}, &environment_sink};
    const auto result = instance.Instantiate(level_id);
    ASSERT_TRUE(result) << result.diagnostic;
    EXPECT_EQ(instance.GetActorCount(), 5U);
    EXPECT_TRUE(instance.FindActor("mesh").has_value());
    EXPECT_TRUE(instance.FindActor("directional").has_value());
    EXPECT_TRUE(instance.FindActor("point").has_value());
    EXPECT_TRUE(instance.FindActor("spot").has_value());
    EXPECT_TRUE(instance.FindActor("camera").has_value());

    ASSERT_EQ(light_sink.creates.size(), 3U);
    const auto &directional_source =
        std::get<kpengine::render::DirectionalLightSourceDesc>(light_sink.creates[0]);
    EXPECT_EQ(directional_source.direction, directional.direction);
    EXPECT_EQ(directional_source.color, directional.color);
    EXPECT_FLOAT_EQ(directional_source.intensity, directional.intensity);
    EXPECT_FALSE(directional_source.enabled);
    EXPECT_FALSE(directional_source.casts_shadow);
    const auto &point_source =
        std::get<kpengine::render::PointLightSourceDesc>(light_sink.creates[1]);
    EXPECT_EQ(point_source.position, point.position);
    EXPECT_FLOAT_EQ(point_source.range, point.range);
    const auto &spot_source =
        std::get<kpengine::render::SpotLightSourceDesc>(light_sink.creates[2]);
    EXPECT_EQ(spot_source.position, spot.position);
    EXPECT_EQ(spot_source.direction, spot.direction);
    EXPECT_FLOAT_EQ(spot_source.inner_cone_radians, spot.inner_cone_radians);
    EXPECT_FLOAT_EQ(spot_source.outer_cone_radians, spot.outer_cone_radians);

    ASSERT_EQ(camera_sink.creates.size(), 1U);
    const auto &camera_source = camera_sink.creates[0];
    EXPECT_EQ(camera_source.world_transform.position_, camera.transform.position);
    EXPECT_EQ(camera_source.world_transform.rotator_,
              (kpengine::Rotatorf{15.0f, 16.0f, 17.0f}));
    EXPECT_EQ(camera_source.world_transform.scale_, camera.transform.scale);
    EXPECT_EQ(camera_source.projection_mode,
              kpengine::render::CameraProjectionMode::Orthographic);
    EXPECT_FLOAT_EQ(camera_source.near_plane, camera.near_plane);
    EXPECT_FLOAT_EQ(camera_source.far_plane, camera.far_plane);
    EXPECT_FLOAT_EQ(camera_source.orthographic_height, camera.orthographic_height);
    EXPECT_EQ(camera_source.priority, camera.priority);

    ASSERT_EQ(environment_sink.creates.size(), 1U);
    EXPECT_EQ(environment_sink.creates[0].texture_asset, assets.environment_texture_id);
    EXPECT_FLOAT_EQ(environment_sink.creates[0].ibl_intensity, environment.ibl_intensity);

    instance.Unload();
    ASSERT_FALSE(event_log.events.empty());
    EXPECT_EQ(event_log.events.back(), "renderable_destroy");
    EXPECT_EQ(event_log.events[event_log.events.size() - 6], "environment_destroy");
    EXPECT_EQ(environment_sink.destroys.size(), 1U);
    EXPECT_EQ(light_sink.destroys.size(), 3U);
    EXPECT_EQ(camera_sink.destroys.size(), 1U);
}

TEST(RuntimeLevelTest, EnvironmentRegistrationFailureRollsBackMixedActorsAndRetries)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingLightSink light_sink;
    RecordingCameraSink camera_sink;
    RecordingEnvironmentSink environment_sink;
    environment_sink.reject_creates = true;
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink, &camera_sink};

    kpengine::asset::LevelDirectionalLightRecord light{};
    light.id = "light";
    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    const AssetID level_id = assets.AddLevel(
        {MakeMeshRecord("mesh"), light},
        {assets.model_id, assets.material_id, assets.environment_texture_id}, environment);

    kpengine::runtime::LevelInstance instance{assets.assets, world, {}, &environment_sink};
    const auto failed = instance.Instantiate(level_id);
    EXPECT_EQ(failed.error, kpengine::runtime::LevelInstanceError::EnvironmentSourceCreationFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(source_sink.destroys.size() == 1U);
    EXPECT_TRUE(light_sink.destroys.size() == 1U);

    environment_sink.reject_creates = false;
    const auto retry = instance.Instantiate(level_id);
    ASSERT_TRUE(retry) << retry.diagnostic;
    EXPECT_EQ(instance.GetActorCount(), 2U);
    EXPECT_EQ(environment_sink.creates.size(), 1U);
    instance.Unload();
    EXPECT_EQ(environment_sink.destroys.size(), 1U);
}

TEST(RuntimeLevelTest, NonStaticFactoryFailureUsesActorRollbackBoundary)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingLightSink light_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink};
    kpengine::asset::LevelDirectionalLightRecord light{};
    light.id = "light";
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("mesh"), light});

    kpengine::runtime::LevelActorFactorySet factories{};
    factories.directional_light = [](kpengine::gameplay::GameplayWorld &,
                                     const kpengine::gameplay::DirectionalLightActorDesc &)
    { return kpengine::gameplay::ActorHandle{}; };
    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factories)};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::ActorCreationFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_EQ(source_sink.creates.size(), 1U);
    EXPECT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(light_sink.creates.size(), 0U);
}

TEST(RuntimeLevelTest, PointFactoryFailureRollsBackAndRetriesImmediately)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingLightSink light_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink};
    kpengine::asset::LevelPointLightRecord point{};
    point.id = "point";
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("mesh"), point});

    bool fail_once = true;
    kpengine::runtime::LevelActorFactorySet factories{};
    factories.point_light = [&fail_once](
        kpengine::gameplay::GameplayWorld &gameplay_world,
        const kpengine::gameplay::PointLightActorDesc &description)
    {
        if (fail_once)
        {
            fail_once = false;
            return kpengine::gameplay::ActorHandle{};
        }
        return kpengine::gameplay::CreatePointLightActor(gameplay_world, description);
    };

    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factories)};
    const auto failed = instance.Instantiate(level_id);
    EXPECT_EQ(failed.error, kpengine::runtime::LevelInstanceError::ActorCreationFailed);
    EXPECT_NE(failed.diagnostic.find("point-light"), std::string::npos);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_TRUE(instance.FindActor("mesh") == std::nullopt);
    EXPECT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_TRUE(light_sink.creates.empty());

    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_EQ(instance.GetActorCount(), 2U);
    instance.Unload();
}

TEST(RuntimeLevelTest, SpotFactoryFailureRollsBackAndRetriesImmediately)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingLightSink light_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink};
    kpengine::asset::LevelSpotLightRecord spot{};
    spot.id = "spot";
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("mesh"), spot});

    bool fail_once = true;
    kpengine::runtime::LevelActorFactorySet factories{};
    factories.spot_light = [&fail_once](
        kpengine::gameplay::GameplayWorld &gameplay_world,
        const kpengine::gameplay::SpotLightActorDesc &description)
    {
        if (fail_once)
        {
            fail_once = false;
            return kpengine::gameplay::ActorHandle{};
        }
        return kpengine::gameplay::CreateSpotLightActor(gameplay_world, description);
    };

    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factories)};
    const auto failed = instance.Instantiate(level_id);
    EXPECT_EQ(failed.error, kpengine::runtime::LevelInstanceError::ActorCreationFailed);
    EXPECT_NE(failed.diagnostic.find("spot-light"), std::string::npos);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_TRUE(light_sink.creates.empty());

    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_EQ(instance.GetActorCount(), 2U);
    instance.Unload();
}

TEST(RuntimeLevelTest, CameraFactoryFailureRollsBackAndRetriesImmediately)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingCameraSink camera_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink, nullptr, &camera_sink};
    kpengine::asset::LevelCameraRecord camera{};
    camera.id = "camera";
    const AssetID level_id = assets.AddLevel({MakeMeshRecord("mesh"), camera});

    bool fail_once = true;
    kpengine::runtime::LevelActorFactorySet factories{};
    factories.camera = [&fail_once](
        kpengine::gameplay::GameplayWorld &gameplay_world,
        const kpengine::gameplay::CameraActorDesc &description)
    {
        if (fail_once)
        {
            fail_once = false;
            return kpengine::gameplay::ActorHandle{};
        }
        return kpengine::gameplay::CreateCameraActor(gameplay_world, description);
    };

    kpengine::runtime::LevelInstance instance{assets.assets, world, std::move(factories)};
    const auto failed = instance.Instantiate(level_id);
    EXPECT_EQ(failed.error, kpengine::runtime::LevelInstanceError::ActorCreationFailed);
    EXPECT_NE(failed.diagnostic.find("camera"), std::string::npos);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_TRUE(camera_sink.creates.empty());

    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_EQ(instance.GetActorCount(), 2U);
    instance.Unload();
}

TEST(RuntimeLevelTest, MissingEnvironmentSinkRollsBackActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    const AssetID level_id = assets.AddLevel(
        {MakeMeshRecord("mesh")},
        {assets.model_id, assets.material_id, assets.environment_texture_id}, environment);

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error,
              kpengine::runtime::LevelInstanceError::EnvironmentSourceCreationFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_EQ(source_sink.creates.size(), 1U);
    EXPECT_EQ(source_sink.destroys.size(), 1U);
}

TEST(RuntimeLevelTest, OccupiedEnvironmentSinkRollsBackActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink};
    kpengine::render::EnvironmentSourceRegistry environment_sink;
    const auto occupied = environment_sink.EnqueueCreate(
        {assets.environment_texture_id, 1.0f});
    ASSERT_TRUE(occupied.IsValid());
    environment_sink.Drain();

    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    const AssetID level_id = assets.AddLevel(
        {MakeMeshRecord("mesh")},
        {assets.model_id, assets.material_id, assets.environment_texture_id}, environment);

    kpengine::runtime::LevelInstance instance{assets.assets, world, {}, &environment_sink};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error,
              kpengine::runtime::LevelInstanceError::EnvironmentSourceCreationFailed);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_EQ(source_sink.creates.size(), 1U);
    EXPECT_EQ(source_sink.destroys.size(), 1U);
    EXPECT_EQ(environment_sink.GetActiveHandle(), occupied);

    ASSERT_TRUE(environment_sink.EnqueueDestroy(occupied));
    environment_sink.Drain();
}

TEST(RuntimeLevelTest, InstantiatesEnvironmentOnlyLevel)
{
    AssetFixture assets;
    kpengine::gameplay::GameplayWorld world{};
    kpengine::render::EnvironmentSourceRegistry environment_sink;
    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    environment.ibl_intensity = 1.5f;
    const AssetID level_id = assets.AddLevel(
        {}, {assets.model_id, assets.material_id, assets.environment_texture_id}, environment);

    kpengine::runtime::LevelInstance instance{assets.assets, world, {}, &environment_sink};
    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_TRUE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    environment_sink.Drain();
    ASSERT_TRUE(environment_sink.GetActiveSource().has_value());
    EXPECT_EQ(environment_sink.GetActiveSource()->texture_asset, assets.environment_texture_id);
    EXPECT_FLOAT_EQ(environment_sink.GetActiveSource()->ibl_intensity, 1.5f);
    instance.Unload();
    environment_sink.Drain();
    EXPECT_FALSE(environment_sink.GetActiveSource().has_value());
}

TEST(RuntimeLevelTest, RejectsEnvironmentWithoutTexturePayloadBeforeCreatingActors)
{
    AssetFixture assets;
    RecordingSourceSink source_sink;
    RecordingLightSink light_sink;
    kpengine::gameplay::GameplayWorld world{&source_sink, &light_sink};

    auto invalid_texture = std::make_shared<kpengine::asset::TextureResource>();
    invalid_texture->data.reset();
    AssetRegisterInfo invalid_texture_info{};
    invalid_texture_info.resource = std::move(invalid_texture);
    invalid_texture_info.path = "runtime_level_invalid_environment.texture";
    invalid_texture_info.name = "RuntimeLevelInvalidEnvironment";
    invalid_texture_info.type = AssetType::KPAT_Texture;
    const AssetID invalid_texture_id = assets.AddExtraAsset(std::move(invalid_texture_info));

    kpengine::asset::LevelDirectionalLightRecord light{};
    light.id = "light";
    kpengine::asset::LevelEnvironmentRecord environment{};
    environment.texture.dependency_index = 2;
    const AssetID level_id = assets.AddLevel(
        {light}, {assets.model_id, assets.material_id, invalid_texture_id}, environment);

    RecordingEnvironmentSink environment_sink;
    kpengine::runtime::LevelInstance instance{assets.assets, world, {}, &environment_sink};
    const auto result = instance.Instantiate(level_id);
    EXPECT_EQ(result.error, kpengine::runtime::LevelInstanceError::InvalidEnvironmentResource);
    EXPECT_FALSE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    EXPECT_TRUE(light_sink.creates.empty());
    EXPECT_TRUE(environment_sink.creates.empty());
}
