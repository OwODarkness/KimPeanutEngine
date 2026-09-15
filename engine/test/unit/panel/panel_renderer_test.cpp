#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "dot_matrix.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "panel.h"
#include "panel_renderer.h"
#include "render/frame_context.h"
#include "support/fake_render_backend.h"
#include "test_glyphs.h"

namespace
{
    using kpengine::graphics::CommandRecorder;
    using kpengine::graphics::RenderBackend;
    using kpengine::panel::DotMatrix;
    using kpengine::panel::Panel;
    using kpengine::panel::PanelRenderPlanOptions;
    using kpengine::panel::PanelRenderer;
    using kpengine::test::BackendProbe;
    using kpengine::test::FakeBackend;

    constexpr std::uint32_t kInitialWidth = 256u;
    constexpr std::uint32_t kInitialHeight = 128u;
    constexpr std::uint32_t kResizedWidth = 320u;
    constexpr std::uint32_t kResizedHeight = 64u;
    constexpr std::size_t kUniformCapacity = 1u * 1024u * 1024u;

    // A deterministic dot pattern of known extent: two halfwidth characters in
    // a 32x16 dot panel.
    DotMatrix MakePattern()
    {
        Panel panel{2u, 1u};
        panel.SetText(0u, "AB");
        return panel.Rebuild(kpengine::panel::test::MakeHalfwidthSet(
            static_cast<std::uint32_t>('A'), 2u));
    }

    class PanelRendererTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            backend.Initialize({});
            if (!renderer.Initialize(backend, kInitialWidth, kInitialHeight, diagnostic))
            {
                GTEST_SKIP() << "the panel shader program is unavailable: " << diagnostic;
            }
            frame.Initialize(backend, kUniformCapacity);
        }

        bool RecordFrame(const PanelRenderPlanOptions &options = {})
        {
            CommandRecorder *const recorder = backend.GetCommandRecorder();
            if (recorder == nullptr)
            {
                diagnostic = "fake backend exposed no command recorder";
                return false;
            }
            // The executor refuses a submission without an active frame, so the
            // bracket is part of the production sequence rather than test
            // scaffolding.
            frame.Begin(0u, {frame_number, 0.0f, 1.0f / 60.0f}, backend.GetRenderExtent());
            const bool recorded = renderer.Record(frame, *recorder, options, diagnostic);
            frame.End();
            ++frame_number;
            return recorded;
        }

        std::shared_ptr<BackendProbe> probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        kpengine::render::FrameContext frame;
        PanelRenderer renderer;
        std::string diagnostic;
        std::uint64_t frame_number = 0u;
    };

    TEST_F(PanelRendererTest, InitializesEveryHandleItOwns)
    {
        EXPECT_GT(renderer.GetLiveGpuHandleCount(), 0u);
        EXPECT_TRUE(renderer.GetOutputTarget().IsValid());
        EXPECT_EQ(renderer.GetOutputView().width, kInitialWidth);
        EXPECT_EQ(renderer.GetOutputView().height, kInitialHeight);
        // Until a panel is uploaded there is nothing to sample, which is what
        // Record reports rather than drawing an empty quad.
        EXPECT_FALSE(renderer.HasDotMask());
    }

    TEST_F(PanelRendererTest, RecordingWithoutAPanelIsRejected)
    {
        EXPECT_FALSE(RecordFrame());
        EXPECT_FALSE(diagnostic.empty());
        EXPECT_EQ(probe->draw_count, 0);
    }

    TEST_F(PanelRendererTest, UploadingAPanelThenRecordingDrawsOneQuad)
    {
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;
        EXPECT_TRUE(renderer.HasDotMask());

        const int draws_before = probe->draw_count;
        ASSERT_TRUE(RecordFrame()) << diagnostic;

        EXPECT_EQ(probe->draw_count, draws_before + 1);
        ASSERT_FALSE(probe->viewports.empty());
        EXPECT_FLOAT_EQ(probe->viewports.back().width,
                        static_cast<float>(kInitialWidth));
        EXPECT_FLOAT_EQ(probe->viewports.back().height,
                        static_cast<float>(kInitialHeight));
    }

    TEST_F(PanelRendererTest, ReplacingThePanelReleasesThePreviousMask)
    {
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;
        const std::uint32_t handles_after_first = renderer.GetLiveGpuHandleCount();
        const int creates_after_first = probe->texture_create_count;
        const int waits_after_first = probe->wait_idle_count;

        // A content change recreates the texture, because the backend has no
        // in-place upload. If the previous one were not released the count
        // would grow with every upload.
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;

        EXPECT_EQ(probe->texture_create_count, creates_after_first + 1);
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), handles_after_first)
            << "the replaced dot mask was not released";
        // The release is fenced: submitted work may still reference the old
        // texture, so the swap must wait for the device first.
        EXPECT_GT(probe->wait_idle_count, waits_after_first);
    }

    TEST_F(PanelRendererTest, MeasuresTheLitExtentForTheColourRamp)
    {
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;

        // The pattern is two halfwidth characters at the left of a 32x16 dot
        // panel, so the extent must cover them and stop short of the panel edge.
        // A ramp across the whole panel was measured to move the colour almost
        // not at all across a short line.
        const std::array<float, 4> &bounds = renderer.GetInkBounds();
        EXPECT_FLOAT_EQ(bounds[0], 0.0f);
        EXPECT_GT(bounds[2], 0.0f);
        EXPECT_LT(bounds[2], 1.0f) << "the ramp spans the whole panel, not the text";
        EXPECT_LT(bounds[1], bounds[3]);
        EXPECT_LE(bounds[3], 1.0f);
    }

    TEST_F(PanelRendererTest, ABlankPanelKeepsTheWholePanelAsItsExtent)
    {
        // A blank panel has no extent to ramp across, so it must keep a sane one
        // rather than an inverted or zero-width one.
        const DotMatrix blank{32u, 16u};
        ASSERT_TRUE(renderer.UploadPanel(blank, diagnostic)) << diagnostic;

        const std::array<float, 4> &bounds = renderer.GetInkBounds();
        EXPECT_FLOAT_EQ(bounds[0], 0.0f);
        EXPECT_FLOAT_EQ(bounds[1], 0.0f);
        EXPECT_FLOAT_EQ(bounds[2], 1.0f);
        EXPECT_FLOAT_EQ(bounds[3], 1.0f);
    }

    TEST_F(PanelRendererTest, ResizeOutputIsTransactional)
    {
        const auto initial_target = renderer.GetOutputTarget();
        ASSERT_TRUE(initial_target.IsValid());

        // A zero extent is rejected without touching the current target.
        EXPECT_FALSE(renderer.ResizeOutput(0u, kResizedHeight, diagnostic));
        EXPECT_EQ(renderer.GetOutputTarget().id, initial_target.id);
        EXPECT_EQ(renderer.GetOutputView().width, kInitialWidth);

        const int targets_before = static_cast<int>(probe->targets.size());
        ASSERT_TRUE(renderer.ResizeOutput(kResizedWidth, kResizedHeight, diagnostic))
            << diagnostic;
        EXPECT_EQ(renderer.GetOutputView().width, kResizedWidth);
        EXPECT_EQ(renderer.GetOutputView().height, kResizedHeight);
        EXPECT_NE(renderer.GetOutputTarget().id, initial_target.id);
        // The replacement is created alongside the old target, never in place
        // of it, so a failed create can leave the old one valid.
        EXPECT_EQ(static_cast<int>(probe->targets.size()), targets_before + 1);

        // The same extent is a no-op and destroys nothing.
        const int destroys_before = probe->render_target_destroy_count;
        ASSERT_TRUE(renderer.ResizeOutput(kResizedWidth, kResizedHeight, diagnostic));
        EXPECT_EQ(probe->render_target_destroy_count, destroys_before);
    }

    TEST_F(PanelRendererTest, FailedTargetCreationKeepsThePreviousTarget)
    {
        const auto initial_target = renderer.GetOutputTarget();
        ASSERT_TRUE(initial_target.IsValid());

        probe->fail_render_target = true;
        EXPECT_FALSE(renderer.ResizeOutput(kResizedWidth, kResizedHeight, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        // Still usable at the old extent rather than left without output.
        EXPECT_EQ(renderer.GetOutputTarget().id, initial_target.id);
        EXPECT_EQ(renderer.GetOutputView().width, kInitialWidth);

        probe->fail_render_target = false;
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;
        EXPECT_TRUE(RecordFrame()) << diagnostic;
    }

    TEST_F(PanelRendererTest, FailedMaskUploadKeepsTheRendererUsable)
    {
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;
        const std::uint32_t handles_before = renderer.GetLiveGpuHandleCount();

        probe->fail_texture_creation = true;
        EXPECT_FALSE(renderer.UploadPanel(pattern, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        probe->fail_texture_creation = false;

        // The previous mask survived the failed replacement.
        EXPECT_TRUE(renderer.HasDotMask());
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), handles_before);
        EXPECT_TRUE(RecordFrame()) << diagnostic;
    }

    TEST_F(PanelRendererTest, InitializeFailsWhenTheSamplerCannotBeCreated)
    {
        auto failing_probe = std::make_shared<BackendProbe>();
        failing_probe->fail_sampler_creation = true;
        FakeBackend failing_backend{failing_probe};
        failing_backend.Initialize({});

        PanelRenderer failing_renderer;
        std::string failing_diagnostic;
        EXPECT_FALSE(failing_renderer.Initialize(failing_backend, kInitialWidth,
                                                 kInitialHeight, failing_diagnostic));
        EXPECT_FALSE(failing_diagnostic.empty());
        // A failed initialize must not leave handles behind.
        EXPECT_EQ(failing_renderer.GetLiveGpuHandleCount(), 0u);
    }

    TEST_F(PanelRendererTest, CleanupReleasesEveryHandleItOwned)
    {
        const DotMatrix pattern = MakePattern();
        ASSERT_TRUE(renderer.UploadPanel(pattern, diagnostic)) << diagnostic;
        ASSERT_TRUE(RecordFrame()) << diagnostic;

        EXPECT_GT(renderer.GetLiveGpuHandleCount(), 0u);
        renderer.Cleanup();

        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), 0u);
        EXPECT_FALSE(renderer.GetOutputTarget().IsValid());
        EXPECT_FALSE(renderer.HasDotMask());
    }

    TEST_F(PanelRendererTest, CleanupIsIdempotent)
    {
        renderer.Cleanup();
        renderer.Cleanup();
        EXPECT_EQ(renderer.GetLiveGpuHandleCount(), 0u);
    }
}
