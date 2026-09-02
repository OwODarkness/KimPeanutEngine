#include <limits>

#include <gtest/gtest.h>

#include "render/environment_source_registry.h"

namespace
{
    kpengine::render::EnvironmentSourceDesc MakeSource(uint32_t id, float intensity = 1.0f)
    {
        return {{id, 0, kpengine::asset::AssetType::KPAT_Texture}, intensity};
    }
}

TEST(EnvironmentSourceRegistryTest, OwnsOneSourceAndPublishesAtDrain)
{
    kpengine::render::EnvironmentSourceRegistry registry;
    const auto first = registry.EnqueueCreate(MakeSource(3));
    ASSERT_TRUE(first.IsValid());
    EXPECT_FALSE(registry.GetActiveSource().has_value());
    EXPECT_FALSE(registry.EnqueueCreate(MakeSource(4)).IsValid());

    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());
    EXPECT_EQ(registry.GetActiveSource()->texture_asset.id, 3U);
    EXPECT_EQ(registry.GetActiveHandle(), first);

    ASSERT_TRUE(registry.EnqueueDestroy(first));
    const auto second = registry.EnqueueCreate(MakeSource(4, 2.0f));
    ASSERT_TRUE(second.IsValid());
    EXPECT_NE(second.generation, first.generation);
    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());
    EXPECT_EQ(registry.GetActiveSource()->texture_asset.id, 4U);
    EXPECT_FLOAT_EQ(registry.GetActiveSource()->ibl_intensity, 2.0f);
    EXPECT_EQ(registry.GetActiveHandle(), second);

    EXPECT_FALSE(registry.EnqueueDestroy(first));
    EXPECT_TRUE(registry.EnqueueDestroy(second));
    registry.Drain();
    EXPECT_FALSE(registry.GetActiveSource().has_value());
    EXPECT_FALSE(registry.GetActiveHandle().has_value());
}

TEST(EnvironmentSourceRegistryTest, RejectsInvalidDescriptorsAndStaleDestroys)
{
    kpengine::render::EnvironmentSourceRegistry registry;
    EXPECT_FALSE(registry.EnqueueCreate({{}, 1.0f}).IsValid());
    EXPECT_FALSE(registry.EnqueueCreate(
                         {{1, 0, kpengine::asset::AssetType::KPAT_Model}, 1.0f})
                     .IsValid());
    EXPECT_FALSE(registry.EnqueueCreate(
                         {MakeSource(1).texture_asset,
                          std::numeric_limits<float>::quiet_NaN()})
                     .IsValid());
    EXPECT_FALSE(registry.EnqueueCreate({MakeSource(1).texture_asset, -1.0f}).IsValid());

    const auto handle = registry.EnqueueCreate(MakeSource(1));
    ASSERT_TRUE(handle.IsValid());
    EXPECT_FALSE(registry.EnqueueDestroy({handle.id, static_cast<uint16_t>(handle.generation + 1)}));
    registry.Clear();
    EXPECT_FALSE(registry.EnqueueDestroy(handle));
    EXPECT_FALSE(registry.GetActiveSource().has_value());
}

TEST(EnvironmentSourceRegistryTest, ClearDropsPendingAndActiveState)
{
    kpengine::render::EnvironmentSourceRegistry registry;
    const auto handle = registry.EnqueueCreate(MakeSource(7));
    ASSERT_TRUE(handle.IsValid());
    registry.Clear();
    registry.Drain();
    EXPECT_FALSE(registry.GetActiveSource().has_value());
    const auto replacement = registry.EnqueueCreate(MakeSource(8));
    EXPECT_TRUE(replacement.IsValid());
}

TEST(EnvironmentSourceRegistryTest, ClearInvalidatesOldHandleGeneration)
{
    kpengine::render::EnvironmentSourceRegistry registry;
    const auto old_handle = registry.EnqueueCreate(MakeSource(7));
    ASSERT_TRUE(old_handle.IsValid());

    registry.Clear();
    const auto replacement = registry.EnqueueCreate(MakeSource(8));
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_FALSE(replacement == old_handle);
    EXPECT_FALSE(registry.EnqueueDestroy(old_handle));

    registry.Drain();
    ASSERT_TRUE(registry.GetActiveSource().has_value());
    EXPECT_EQ(registry.GetActiveSource()->texture_asset.id, 8U);
    EXPECT_EQ(registry.GetActiveHandle(), replacement);
}
