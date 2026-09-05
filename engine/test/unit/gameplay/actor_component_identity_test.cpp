#include <gtest/gtest.h>

#include "gameplay/actor/actor.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/component/scene_component.h"

namespace
{
    class TestComponent final : public kpengine::gameplay::ActorComponent
    {
    };
}

TEST(ActorComponentIdentityTest, DuplicateTypesReceiveStableDistinctIds)
{
    kpengine::gameplay::Actor actor({1U, 0U});
    auto *const first = actor.AddComponent<TestComponent>();
    auto *const second = actor.AddComponent<TestComponent>();
    auto *const scene = actor.AddComponent<kpengine::gameplay::SceneComponent>();

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(scene, nullptr);
    EXPECT_TRUE(first->GetInstanceId().IsValid());
    EXPECT_TRUE(second->GetInstanceId().IsValid());
    EXPECT_TRUE(scene->GetInstanceId().IsValid());
    EXPECT_NE(first->GetInstanceId(), second->GetInstanceId());
    EXPECT_NE(second->GetInstanceId(), scene->GetInstanceId());
    EXPECT_EQ(first->GetInstanceId().value, 1U);
    EXPECT_EQ(second->GetInstanceId().value, 2U);
    EXPECT_EQ(scene->GetInstanceId().value, 3U);
}

TEST(ActorComponentIdentityTest, ComponentIdsRemainStableAfterVectorGrowth)
{
    kpengine::gameplay::Actor actor({2U, 0U});
    auto *const first = actor.AddComponent<kpengine::gameplay::SceneComponent>();
    ASSERT_NE(first, nullptr);
    const kpengine::gameplay::ComponentInstanceId first_id = first->GetInstanceId();

    for (int index = 0; index < 32; ++index)
    {
        ASSERT_NE(actor.AddComponent<kpengine::gameplay::MeshComponent>(), nullptr);
    }

    EXPECT_EQ(first->GetInstanceId(), first_id);
}
