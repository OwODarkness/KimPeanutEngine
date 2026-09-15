#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bubble_renderer.h"
#include "dot_matrix.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "support/fake_render_backend.h"

namespace
{
    using kpengine::graphics::RenderBackend;
    using kpengine::graphics::Viewport;
    using kpengine::live2d::BubbleAppearance;
    using kpengine::live2d::BubbleLayout;
    using kpengine::live2d::BubbleLayoutRequest;
    using kpengine::live2d::BubblePlacement;
    using kpengine::live2d::BubbleRenderer;
    using kpengine::live2d::BubbleTail;
    using kpengine::panel::DotMatrix;
    using kpengine::panel::GlyphCell;
    using kpengine::panel::MergeOp;
    using kpengine::test::BackendProbe;
    using kpengine::test::FakeBackend;

    // A solid block of ink, which is all the bubble needs: it never interprets
    // the text, it only samples it.
    DotMatrix MakeText()
    {
        DotMatrix matrix{64u, 16u};
        GlyphCell glyph;
        glyph.advance = 16u;
        for (std::uint32_t row = 0u; row < kpengine::panel::kGlyphRows; ++row)
        {
            for (std::uint32_t column = 0u; column < kpengine::panel::kGlyphColumns;
                 ++column)
            {
                glyph.SetDot(row, column, true);
            }
        }
        matrix.StampGlyph(glyph, 0u, 0u, MergeOp::Replace);
        return matrix;
    }

    BubbleLayout MakeLayout()
    {
        BubbleLayoutRequest request;
        request.text_width = 48u;
        request.text_height = 16u;
        BubbleLayout layout;
        EXPECT_TRUE(kpengine::live2d::BuildBubbleLayout(request, layout));
        return layout;
    }

    Viewport FullViewport()
    {
        Viewport viewport;
        viewport.width = 720.0f;
        viewport.height = 960.0f;
        return viewport;
    }

    class BubbleRendererTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            backend.Initialize({});
            if (!renderer.Initialize(backend, TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB,
                                     diagnostic))
            {
                GTEST_SKIP() << "the bubble shader program is unavailable: " << diagnostic;
            }
        }

        std::shared_ptr<BackendProbe> probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        BubbleRenderer renderer;
        std::string diagnostic;
    };

    TEST_F(BubbleRendererTest, InitializesEveryHandleItOwns)
    {
        EXPECT_GT(renderer.GetLiveGpuHandleCount(), 0u);
        // Nothing to sample until text is uploaded, which is what BuildDraws
        // reports rather than drawing from an unbound texture.
        EXPECT_FALSE(renderer.HasText());
    }

    TEST_F(BubbleRendererTest, DrawingWithoutTextIsRejected)
    {
        std::vector<kpengine::render::SubmissionDraw> draws;
        EXPECT_FALSE(renderer.BuildDraws(MakeLayout(), BubblePlacement{},
                                         BubbleAppearance{}, FullViewport(), draws,
                                         diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_TRUE(draws.empty());
    }

    TEST_F(BubbleRendererTest, BuildsOneDrawBindingTheConstantsAndTheText)
    {
        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;
        EXPECT_TRUE(renderer.HasText());

        std::vector<kpengine::render::SubmissionDraw> draws;
        ASSERT_TRUE(renderer.BuildDraws(MakeLayout(), BubblePlacement{},
                                        BubbleAppearance{}, FullViewport(), draws,
                                        diagnostic))
            << diagnostic;

        ASSERT_EQ(draws.size(), 1u);
        const kpengine::render::SubmissionDraw &draw = draws.front();
        EXPECT_TRUE(draw.pipeline.IsValid());
        ASSERT_EQ(draw.geometry.vertices.size(), 1u);
        ASSERT_EQ(draw.uniforms.size(), 1u);
        ASSERT_EQ(draw.textures.size(), 1u);
        EXPECT_EQ(draw.uniforms.front().set, 0u);
        EXPECT_EQ(draw.uniforms.front().binding,
                  kpengine::live2d::kBubbleConstantsBinding);
        EXPECT_EQ(draw.uniforms.front().bytes.size(),
                  sizeof(kpengine::live2d::BubbleDrawConstants));
        EXPECT_EQ(draw.textures.front().binding,
                  kpengine::live2d::kBubbleTextBinding);
        EXPECT_TRUE(draw.textures.front().sampler.IsValid());
        EXPECT_EQ(draw.index_count, 6u);
        // The viewport is the pass's, not the bubble's: placement is a matrix,
        // because the backends measure a viewport's y from opposite ends.
        EXPECT_FLOAT_EQ(draw.viewport.width, 720.0f);
        EXPECT_FLOAT_EQ(draw.viewport.height, 960.0f);
    }

    TEST_F(BubbleRendererTest, ABubbleThatHasNotAppearedIsNotDrawn)
    {
        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;

        BubblePlacement placement;
        placement.progress = 0.0f;
        std::vector<kpengine::render::SubmissionDraw> draws;
        // A success with no draw: a zero-sized quad would validate and produce
        // nothing, which reads as a bubble that failed rather than one that has
        // not arrived.
        EXPECT_TRUE(renderer.BuildDraws(MakeLayout(), placement, BubbleAppearance{},
                                        FullViewport(), draws, diagnostic))
            << diagnostic;
        EXPECT_TRUE(draws.empty());
    }

    TEST_F(BubbleRendererTest, ReplacingTheTextReleasesThePreviousMask)
    {
        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;
        const std::uint32_t handles = renderer.GetLiveGpuHandleCount();
        const int creates = probe->texture_create_count;
        const int waits = probe->wait_idle_count;

        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;

        EXPECT_EQ(probe->texture_create_count, creates + 1);
        // If the previous mask were not released the count would grow with every
        // upload. The probe records no texture-destroy event, so a stable count
        // is the stronger statement anyway.
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), handles)
            << "the replaced text mask was not released";
        EXPECT_GT(probe->wait_idle_count, waits);
    }

    TEST_F(BubbleRendererTest, FailedTextUploadKeepsTheRendererUsable)
    {
        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;
        const std::uint32_t handles = renderer.GetLiveGpuHandleCount();

        probe->fail_texture_creation = true;
        EXPECT_FALSE(renderer.UploadText(MakeText(), diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        probe->fail_texture_creation = false;

        EXPECT_TRUE(renderer.HasText());
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), handles);

        std::vector<kpengine::render::SubmissionDraw> draws;
        EXPECT_TRUE(renderer.BuildDraws(MakeLayout(), BubblePlacement{},
                                        BubbleAppearance{}, FullViewport(), draws,
                                        diagnostic))
            << diagnostic;
        EXPECT_EQ(draws.size(), 1u);
    }

    TEST_F(BubbleRendererTest, InitializeFailsWhenTheSamplerCannotBeCreated)
    {
        auto failing_probe = std::make_shared<BackendProbe>();
        failing_probe->fail_sampler_creation = true;
        FakeBackend failing_backend{failing_probe};
        failing_backend.Initialize({});

        BubbleRenderer failing_renderer;
        std::string failing_diagnostic;
        EXPECT_FALSE(failing_renderer.Initialize(
            failing_backend, TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB, failing_diagnostic));
        EXPECT_FALSE(failing_diagnostic.empty());
        EXPECT_EQ(failing_renderer.GetLiveGpuHandleCount(), 0u);
    }

    TEST_F(BubbleRendererTest, CleanupReleasesEveryHandleItOwned)
    {
        ASSERT_TRUE(renderer.UploadText(MakeText(), diagnostic)) << diagnostic;
        EXPECT_GT(renderer.GetLiveGpuHandleCount(), 0u);

        renderer.Cleanup();

        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), 0u);
        EXPECT_FALSE(renderer.HasText());
    }

    TEST_F(BubbleRendererTest, CleanupIsIdempotent)
    {
        renderer.Cleanup();
        renderer.Cleanup();
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), 0u);
    }
}
