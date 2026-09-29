#include <gtest/gtest.h>

#include <utility>

#include <vector>

#include "render/passes/scene_draw_recorder.h"

TEST(SceneDrawRecorderTest, OwnsAndReplacesFrameSnapshot)
{
    using namespace kpengine::render;

    SceneDrawRecorder recorder;
    std::vector<MeshProxy> first_snapshot(2);
    first_snapshot[0].handle = {7u, 1u};
    first_snapshot[1].handle = {8u, 3u};
    recorder.BeginFrame(std::move(first_snapshot), 12u);

    ASSERT_EQ(recorder.Snapshot().size(), 2u);
    EXPECT_EQ(recorder.Snapshot()[0].handle.id, 7u);
    EXPECT_EQ(recorder.Snapshot()[1].handle.generation, 3u);

    std::vector<MeshProxy> next_snapshot(1);
    next_snapshot[0].handle = {9u, 2u};
    recorder.BeginFrame(std::move(next_snapshot), 13u);
    ASSERT_EQ(recorder.Snapshot().size(), 1u);
    EXPECT_EQ(recorder.Snapshot()[0].handle.id, 9u);

    recorder.Clear();
    recorder.Clear();
    EXPECT_TRUE(recorder.Snapshot().empty());
}
