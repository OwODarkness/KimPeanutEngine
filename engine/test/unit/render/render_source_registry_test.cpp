#include <gtest/gtest.h>

#include "render/render_source_registry.h"

namespace
{
    kpengine::render::StaticMeshRenderableSourceDesc MakeSource(float x = 0.0f)
    {
        kpengine::render::StaticMeshRenderableSourceDesc source{};
        source.mesh_asset = {1, 0, kpengine::asset::AssetType::KPAT_Mesh};
        source.material_asset = {1, 0, kpengine::asset::AssetType::KPAT_Material};
        source.world_transform.position_ = {x, 0.0f, 0.0f};
        source.world_bounds = {{x - 1.0f, -1.0f, -1.0f}, {x + 1.0f, 1.0f, 1.0f}};
        return source;
    }
}

TEST(RenderableSourceRegistryTest, KeepsPendingSourcesProxyFreeAndAppliesReadyUpdates)
{
    kpengine::render::RenderableSourceRegistry registry{};
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableSourceHandle source_handle =
        registry.EnqueueCreate(MakeSource());

    bool ready = false;
    int resolve_count = 0;
    const auto resolve = [&ready, &resolve_count](const kpengine::render::PrimitiveRenderableSourceDesc &source)
    {
        ++resolve_count;
        if (!ready)
        {
            return kpengine::render::RenderableSourceResolution{
                kpengine::render::RenderableSourceState::Pending, "mesh is loading", std::nullopt};
        }
        const auto &static_mesh =
            std::get<kpengine::render::StaticMeshRenderableSourceDesc>(source);
        kpengine::render::MeshProxyDesc proxy{};
        proxy.mesh = {4, 0};
        proxy.material = {1, 0};
        proxy.world_transform = static_mesh.world_transform;
        proxy.world_bounds = static_mesh.world_bounds;
        proxy.flags = static_mesh.flags;
        proxy.lod_bias = static_mesh.lod_bias;
        return kpengine::render::RenderableSourceResolution{
            kpengine::render::RenderableSourceState::Ready, {}, proxy};
    };

    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    EXPECT_TRUE(world.Snapshot().empty());

    ready = true;
    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, MakeSource(5.0f)));
    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    const auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.front().world_transform.position_, (kpengine::Vector3f{5.0f, 0.0f, 0.0f}));

    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    EXPECT_EQ(resolve_count, 2);

    ASSERT_TRUE(registry.EnqueueDestroy(source_handle));
    EXPECT_FALSE(registry.EnqueueUpdate(source_handle, MakeSource(9.0f)));
    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    EXPECT_TRUE(world.Snapshot().empty());
}

TEST(RenderableSourceRegistryTest, RetiresReadyProxyWhenResolutionFails)
{
    kpengine::render::RenderableSourceRegistry registry{};
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableSourceHandle source_handle = registry.EnqueueCreate(MakeSource());

    bool failed = false;
    const auto resolve = [&failed](const kpengine::render::PrimitiveRenderableSourceDesc &source)
    {
        if (failed)
        {
            return kpengine::render::RenderableSourceResolution{
                kpengine::render::RenderableSourceState::Failed, "material reference failed", std::nullopt};
        }
        const auto &static_mesh = std::get<kpengine::render::StaticMeshRenderableSourceDesc>(source);
        kpengine::render::MeshProxyDesc proxy{};
        proxy.mesh = {4, 0};
        proxy.material = {1, 0};
        proxy.world_transform = static_mesh.world_transform;
        proxy.world_bounds = static_mesh.world_bounds;
        return kpengine::render::RenderableSourceResolution{
            kpengine::render::RenderableSourceState::Ready, {}, proxy};
    };

    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    ASSERT_EQ(world.Snapshot().size(), 1u);

    failed = true;
    ASSERT_TRUE(registry.EnqueueUpdate(source_handle, MakeSource(2.0f)));
    registry.Drain(world, resolve);
    world.ApplyPendingCommands();
    EXPECT_TRUE(world.Snapshot().empty());
}
