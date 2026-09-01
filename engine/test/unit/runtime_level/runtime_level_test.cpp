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
#include "data/mesh.h"
#include "gameplay/actor/actor.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "level/level_instance.h"
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

    class RecordingSourceSink final : public kpengine::render::IRenderableSourceSink
    {
    public:
        kpengine::render::RenderableSourceHandle EnqueueCreate(
            const kpengine::render::PrimitiveRenderableSourceDesc &source) override
        {
            creates.push_back(source);
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
            return true;
        }

        struct Update
        {
            kpengine::render::RenderableSourceHandle handle;
            kpengine::render::PrimitiveRenderableSourceDesc source;
        };

        uint32_t next_handle = 0;
        std::vector<kpengine::render::PrimitiveRenderableSourceDesc> creates;
        std::vector<Update> updates;
        std::vector<kpengine::render::RenderableSourceHandle> destroys;
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
        }

        AssetID AddLevel(std::vector<LevelObject> objects,
                         std::vector<AssetID> dependencies = {})
        {
            auto level = std::make_shared<LevelResource>();
            level->objects = std::move(objects);

            if (dependencies.empty())
            {
                dependencies = {model_id, material_id};
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

TEST(RuntimeLevelTest, SkipsNonMeshRecordsAndAllowsEmptyInstance)
{
    AssetFixture assets;
    kpengine::gameplay::GameplayWorld world{};
    kpengine::asset::LevelDirectionalLightRecord light{};
    light.id = "light";
    const AssetID level_id = assets.AddLevel({light});

    kpengine::runtime::LevelInstance instance{assets.assets, world};
    ASSERT_TRUE(instance.Instantiate(level_id));
    EXPECT_TRUE(instance.IsActive());
    EXPECT_EQ(instance.GetActorCount(), 0U);
    instance.Unload();
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
