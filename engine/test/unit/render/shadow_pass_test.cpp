#include <memory>

#include <gtest/gtest.h>

#include "render/passes/shadow_pass.h"
#include "support/fake_render_backend.h"

namespace kpengine::render
{
    TEST(ShadowPassResourceTest, RetriesPartialSamplerCreationAndCleansUpIdempotently)
    {
        const auto probe = std::make_shared<test::BackendProbe>();
        probe->fail_sampler_creation_after = 1;
        test::FakeBackend backend(probe);
        ShadowPass pass;

        EXPECT_FALSE(pass.PrepareSamplers(backend));
        EXPECT_EQ(probe->sampler_create_count, 3);
        EXPECT_EQ(probe->sampler_destroy_count, 0);

        probe->fail_sampler_creation_after = -1;
        EXPECT_TRUE(pass.PrepareSamplers(backend));
        EXPECT_EQ(probe->sampler_create_count, 5);

        pass.Cleanup(backend);
        pass.Cleanup(backend);
        EXPECT_EQ(probe->sampler_destroy_count, 3);
    }
}
