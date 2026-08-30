#include <gtest/gtest.h>

#include <limits>

#include "render/camera_source_registry.h"

namespace
{
    kpengine::render::CameraSourceDesc MakeSource(float x, int priority, bool enabled = true)
    {
        kpengine::render::CameraSourceDesc source{};
        source.world_transform.position_ = {x, 0.0f, 0.0f};
        source.priority = priority;
        source.enabled = enabled;
        return source;
    }
}

TEST(CameraSourceRegistryTest, SelectsHighestPriorityEnabledCopiedSource)
{
    kpengine::render::CameraSourceRegistry registry{};
    const kpengine::render::CameraSourceHandle low =
        registry.EnqueueCreate(MakeSource(1.0f, 1));
    const kpengine::render::CameraSourceHandle high =
        registry.EnqueueCreate(MakeSource(2.0f, 2));
    ASSERT_TRUE(registry.EnqueueUpdate(high, MakeSource(5.0f, 4)));

    registry.Drain();
    auto active = registry.GetActiveSource();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->world_transform.position_, (kpengine::Vector3f{5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(active->priority, 4);

    auto disabled = MakeSource(6.0f, 4, false);
    ASSERT_TRUE(registry.EnqueueUpdate(high, disabled));
    registry.Drain();
    active = registry.GetActiveSource();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->world_transform.position_, (kpengine::Vector3f{1.0f, 0.0f, 0.0f}));

    ASSERT_TRUE(registry.EnqueueDestroy(low));
    registry.Drain();
    EXPECT_FALSE(registry.GetActiveSource().has_value());
}

TEST(CameraSourceRegistryTest, RejectsStaleHandlesAndClearsActiveState)
{
    kpengine::render::CameraSourceRegistry registry{};
    const kpengine::render::CameraSourceHandle original =
        registry.EnqueueCreate(MakeSource(1.0f, 0));
    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());

    ASSERT_TRUE(registry.EnqueueDestroy(original));
    registry.Drain();
    const kpengine::render::CameraSourceHandle replacement =
        registry.EnqueueCreate(MakeSource(2.0f, 0));
    EXPECT_EQ(replacement.id, original.id);
    EXPECT_NE(replacement.generation, original.generation);
    EXPECT_FALSE(registry.EnqueueUpdate(original, MakeSource(3.0f, 1)));

    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());
    EXPECT_EQ(registry.GetActiveSource()->world_transform.position_,
              (kpengine::Vector3f{2.0f, 0.0f, 0.0f}));

    registry.Clear();
    EXPECT_FALSE(registry.GetActiveSource().has_value());
    EXPECT_FALSE(registry.EnqueueUpdate(replacement, MakeSource(4.0f, 0)));
}

TEST(CameraSourceRegistryTest, RejectsInvalidLensAndTransformValues)
{
    kpengine::render::CameraSourceRegistry registry{};
    auto invalid = MakeSource(1.0f, 0);
    invalid.near_plane = invalid.far_plane;
    EXPECT_FALSE(registry.EnqueueCreate(invalid).IsValid());

    const kpengine::render::CameraSourceHandle handle =
        registry.EnqueueCreate(MakeSource(2.0f, 0));
    ASSERT_TRUE(handle.IsValid());
    invalid = MakeSource(3.0f, 0);
    invalid.world_transform.position_.x_ = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(registry.EnqueueUpdate(handle, invalid));

    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());
    EXPECT_EQ(registry.GetActiveSource()->world_transform.position_,
              (kpengine::Vector3f{2.0f, 0.0f, 0.0f}));
}
