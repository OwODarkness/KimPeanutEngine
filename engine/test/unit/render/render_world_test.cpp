#include <gtest/gtest.h>

#include "render/render_world/render_world.h"

namespace
{
    kpengine::render::MeshProxyDesc MakeProxyDesc(uint32_t mesh_id = 1)
    {
        kpengine::render::MeshProxyDesc desc{};
        desc.mesh = {mesh_id, 0};
        desc.material = {mesh_id, 0};
        desc.world_bounds = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        return desc;
    }
}

TEST(RenderWorldTest, AppliesCreateOnlyAtTheFrameBoundary)
{
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableHandle handle = world.EnqueueCreate(MakeProxyDesc());

    EXPECT_FALSE(world.IsRegistered(handle));
    EXPECT_TRUE(world.Snapshot().empty());

    world.ApplyPendingCommands();
    ASSERT_TRUE(world.IsRegistered(handle));
    const auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.front().handle, handle);
}

TEST(RenderWorldTest, AppliesQueuedUpdatesInOrderAndSnapshotsByValue)
{
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableHandle handle = world.EnqueueCreate(MakeProxyDesc());
    world.ApplyPendingCommands();

    auto updated = MakeProxyDesc(2);
    updated.flags.visible = false;
    updated.lod_bias = 3;
    ASSERT_TRUE(world.EnqueueUpdate(handle, updated));
    world.ApplyPendingCommands();

    const auto snapshot = world.Snapshot();
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.front().mesh.id, 2);
    EXPECT_FALSE(snapshot.front().flags.visible);
    EXPECT_EQ(snapshot.front().lod_bias, 3);

    updated.lod_bias = 7;
    EXPECT_EQ(snapshot.front().lod_bias, 3);
}

TEST(RenderWorldTest, RejectsForgedAndDestroyedHandles)
{
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableHandle handle = world.EnqueueCreate(MakeProxyDesc());
    const kpengine::render::RenderableHandle forged{
        handle.id, static_cast<uint16_t>(handle.generation + 1)};
    world.ApplyPendingCommands();

    EXPECT_FALSE(world.EnqueueUpdate(forged, MakeProxyDesc()));
    EXPECT_FALSE(world.EnqueueDestroy(forged));
    ASSERT_TRUE(world.EnqueueDestroy(handle));
    world.ApplyPendingCommands();

    EXPECT_FALSE(world.IsRegistered(handle));
    EXPECT_FALSE(world.Find(handle).has_value());
    EXPECT_FALSE(world.EnqueueUpdate(handle, MakeProxyDesc()));
    EXPECT_FALSE(world.EnqueueDestroy(handle));
}

TEST(RenderWorldTest, HonorsCreateUpdateDestroyCommandOrdering)
{
    kpengine::render::RenderWorld world{};
    const kpengine::render::RenderableHandle handle = world.EnqueueCreate(MakeProxyDesc());
    auto updated = MakeProxyDesc(9);
    ASSERT_TRUE(world.EnqueueUpdate(handle, updated));
    ASSERT_TRUE(world.EnqueueDestroy(handle));

    world.ApplyPendingCommands();
    EXPECT_FALSE(world.IsRegistered(handle));
    EXPECT_TRUE(world.Snapshot().empty());
}
