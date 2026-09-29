#include <gtest/gtest.h>

#include <memory>

#include "render/passes/fullscreen_pass_resources.h"
#include "support/fake_render_backend.h"

TEST(FullscreenPassResourcesTest, RetainsPartialAllocationAcrossRetryAndReleasesIt)
{
    using namespace kpengine;
    using namespace kpengine::render;
    using namespace kpengine::test;

    auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    FullscreenPassResources resources;

    probe->fail_sampler_creation = true;
    EXPECT_FALSE(resources.Initialize(backend));
    ASSERT_TRUE(resources.Mesh().IsValid());
    EXPECT_FALSE(resources.LinearSampler().IsValid());
    EXPECT_EQ(probe->mesh_create_count, 1);

    probe->fail_sampler_creation = false;
    EXPECT_TRUE(resources.Initialize(backend));
    EXPECT_TRUE(resources.IsReady());
    EXPECT_EQ(probe->mesh_create_count, 1);
    EXPECT_EQ(probe->sampler_create_count, 2);
    EXPECT_TRUE(resources.Initialize(backend));
    EXPECT_EQ(probe->mesh_create_count, 1);
    EXPECT_EQ(probe->sampler_create_count, 2);

    resources.Cleanup(backend);
    resources.Cleanup(backend);
    EXPECT_FALSE(resources.IsReady());
    EXPECT_EQ(probe->mesh_destroy_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 1);
}

TEST(FullscreenPassResourcesTest, RetainsSamplerWhenMeshCreationMustBeRetried)
{
    using namespace kpengine::render;
    using namespace kpengine::test;

    auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    FullscreenPassResources resources;

    probe->fail_mesh_creation = true;
    EXPECT_FALSE(resources.Initialize(backend));
    EXPECT_FALSE(resources.Mesh().IsValid());
    ASSERT_TRUE(resources.LinearSampler().IsValid());

    probe->fail_mesh_creation = false;
    EXPECT_TRUE(resources.Initialize(backend));
    EXPECT_EQ(probe->sampler_create_count, 1);
    EXPECT_EQ(probe->mesh_create_count, 2);

    resources.Cleanup(backend);
    EXPECT_EQ(probe->mesh_destroy_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 1);
}
