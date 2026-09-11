#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/render_submission_executor.h"
#include "render/render_submission_frame.h"

namespace kpengine::render
{
    namespace
    {
        // One frame-slot allocator with explicit failure injection. Capacity is
        // expressed in uniform allocations so exhaustion is deterministic.
        class FakeSubmissionFrame final : public RenderSubmissionFrame
        {
        public:
            void SetActive(const bool active) { active_ = active; }
            void LimitUniformAllocations(const std::size_t limit)
            {
                uniform_allocation_limit_ = limit;
            }
            void FailBufferWrites(const bool fail) { fail_buffer_writes_ = fail; }
            void FailBindingAllocations(const bool fail)
            {
                fail_binding_allocations_ = fail;
            }

            bool IsActive() const override { return active_; }

            std::size_t UniformAllocationCount() const
            {
                return uniform_allocation_count_;
            }
            std::size_t WriteCount() const { return write_count_; }

            UniformAllocation AllocateUniform(const std::size_t size) override
            {
                if (uniform_allocation_count_ >= uniform_allocation_limit_)
                {
                    return {};
                }
                ++uniform_allocation_count_;
                UniformAllocation allocation{};
                allocation.buffer = {1u, 0u};
                allocation.offset = uniform_allocation_count_ * 16u;
                allocation.range = size;
                allocation.mapped = storage_.data();
                return allocation;
            }

            graphics::DescriptorSetHandle AllocateResourceBindingSet(
                graphics::PipelineHandle,
                const graphics::ResourceBindingSetDesc &) override
            {
                if (fail_binding_allocations_)
                {
                    return {};
                }
                return {binding_set_id_++, 0u};
            }

            bool WriteFrameBuffer(graphics::BufferHandle buffer, std::size_t,
                                  const void *data, std::size_t size) override
            {
                if (fail_buffer_writes_ || !buffer.IsValid() || data == nullptr ||
                    size == 0u)
                {
                    return false;
                }
                ++write_count_;
                return true;
            }

        private:
            std::array<std::byte, 1024> storage_{};
            std::size_t uniform_allocation_count_ = 0u;
            std::size_t uniform_allocation_limit_ = 64u;
            std::size_t write_count_ = 0u;
            std::uint32_t binding_set_id_ = 1u;
            bool active_ = true;
            bool fail_buffer_writes_ = false;
            bool fail_binding_allocations_ = false;
        };

        // Records the target bracket so a test can prove a rejected command
        // never leaves a target open.
        class FakeCommandRecorder final : public graphics::CommandRecorder
        {
        public:
            void RejectPipelineAt(const std::size_t draw_ordinal)
            {
                reject_pipeline_at_ = draw_ordinal;
            }
            void RejectSecondTargetBegin() { reject_second_target_begin_ = true; }

            int open_targets = 0;
            int target_begins = 0;
            int target_ends = 0;
            int presentation_begins = 0;
            int pipeline_binds = 0;
            int geometry_binds = 0;
            int resource_binds = 0;
            int draw_calls = 0;
            std::vector<std::uint64_t> begun_targets;
            std::vector<std::uint32_t> begun_index_counts;

            bool BeginRenderTarget(graphics::RenderTargetHandle target) override
            {
                if (reject_second_target_begin_ && target_begins >= 1)
                {
                    return false;
                }
                ++target_begins;
                ++open_targets;
                begun_targets.push_back(target.id);
                return true;
            }

            bool BeginPresentation(const std::array<float, 4> *) override
            {
                ++presentation_begins;
                ++open_targets;
                return true;
            }

            void EndRenderTarget() override
            {
                ++target_ends;
                if (open_targets > 0)
                {
                    --open_targets;
                }
            }

            bool BindPipeline(graphics::PipelineHandle) override
            {
                if (pipeline_binds == reject_pipeline_at_)
                {
                    return false;
                }
                ++pipeline_binds;
                return true;
            }

            void BindMesh(graphics::MeshHandle) override {}

            bool BindGeometry(const graphics::GeometryView &) override
            {
                ++geometry_binds;
                return true;
            }

            bool BindResourceBindings(graphics::PipelineHandle,
                                      graphics::DescriptorSetHandle,
                                      const graphics::DynamicUniformOffsets &) override
            {
                ++resource_binds;
                return true;
            }

            void SetViewport(const graphics::Viewport &) override {}
            void SetScissor(const graphics::Scissor &) override {}

            void DrawIndexed(std::uint32_t index_count, std::uint32_t,
                             std::uint32_t, std::int32_t, std::uint32_t) override
            {
                ++draw_calls;
                begun_index_counts.push_back(index_count);
            }

        private:
            std::size_t reject_pipeline_at_ = static_cast<std::size_t>(-1);
            bool reject_second_target_begin_ = false;
        };

        SubmissionDraw MakeDraw()
        {
            SubmissionDraw draw{};
            draw.pipeline = {7u, 0u};
            draw.geometry.vertices = {{0u, {2u, 0u}, 0u}};
            draw.geometry.indices = {{3u, 0u}, 0u,
                                     graphics::IndexElementType::UInt16};
            draw.viewport = {0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 1.0f};
            draw.index_count = 3u;
            draw.uniforms.push_back({0u, 0u, {std::byte{1}, std::byte{2}}});
            draw.textures.push_back({0u, 1u, {4u, 0u}, {5u, 0u}});
            return draw;
        }

        RenderSubmission MakeTwoPassSubmission()
        {
            RenderSubmission submission{};
            SubmissionPass first{};
            first.target = {10u, 0u};
            first.draws.push_back(MakeDraw());
            SubmissionPass second{};
            second.target = {11u, 0u};
            second.draws.push_back(MakeDraw());
            submission.passes = {std::move(first), std::move(second)};
            return submission;
        }
    }

    TEST(RenderSubmissionExecutionTest, RecordsEveryAuthoredPassInOrder)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        submission.buffer_writes.push_back({{6u, 0u}, 0u, {std::byte{1}}});
        FakeSubmissionFrame frame;
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        EXPECT_FALSE(result.partial_output);
        EXPECT_EQ(result.pass_count, 2u);
        EXPECT_EQ(result.draw_count, 2u);
        EXPECT_EQ(result.upload_bytes, 1u);
        EXPECT_EQ(result.binding_count, 2u);
        EXPECT_EQ(recorder.begun_targets,
                  (std::vector<std::uint64_t>{10u, 11u}));
        EXPECT_EQ(recorder.open_targets, 0);
        EXPECT_EQ(recorder.draw_calls, 2);
    }

    TEST(RenderSubmissionExecutionTest, RejectsStructurallyInvalidWorkBeforeRecording)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        submission.passes[1].draws[0].index_count = 0u;
        FakeSubmissionFrame frame;
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(result.partial_output);
        EXPECT_EQ(result.pass_count, 0u);
        EXPECT_EQ(result.draw_count, 0u);
        EXPECT_EQ(result.binding_count, 0u);
        EXPECT_EQ(frame.UniformAllocationCount(), 0u);
        EXPECT_EQ(recorder.target_begins, 0);
        EXPECT_EQ(recorder.presentation_begins, 0);
        EXPECT_EQ(recorder.draw_calls, 0);
    }

    TEST(RenderSubmissionExecutionTest, UniformExhaustionRecordsNoPasses)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        FakeSubmissionFrame frame;
        frame.LimitUniformAllocations(1u);
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(result.partial_output);
        EXPECT_EQ(result.pass_count, 0u);
        EXPECT_EQ(result.draw_count, 0u);
        EXPECT_EQ(recorder.target_begins, 0);
        EXPECT_EQ(recorder.target_ends, 0);
        // The frame bracket is untouched, so the caller can still end it.
        EXPECT_TRUE(frame.IsActive());
    }

    TEST(RenderSubmissionExecutionTest, BindingAllocationFailureRecordsNoPasses)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        FakeSubmissionFrame frame;
        frame.FailBindingAllocations(true);
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(result.partial_output);
        EXPECT_EQ(result.pass_count, 0u);
        EXPECT_EQ(result.binding_count, 0u);
        EXPECT_EQ(recorder.target_begins, 0);
    }

    TEST(RenderSubmissionExecutionTest, UploadFailureRecordsNoPasses)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        submission.buffer_writes.push_back({{6u, 0u}, 0u, {std::byte{1}}});
        FakeSubmissionFrame frame;
        frame.FailBufferWrites(true);
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_FALSE(result.partial_output);
        EXPECT_EQ(result.pass_count, 0u);
        EXPECT_EQ(result.upload_bytes, 0u);
        EXPECT_EQ(recorder.target_begins, 0);
    }

    TEST(RenderSubmissionExecutionTest, InactiveFrameRecordsNothing)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        FakeSubmissionFrame frame;
        frame.SetActive(false);
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_EQ(result.pass_count, 0u);
        EXPECT_EQ(recorder.target_begins, 0);
    }

    TEST(RenderSubmissionExecutionTest, RejectedBindingClosesTheActiveTarget)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        FakeSubmissionFrame frame;
        FakeCommandRecorder recorder;
        // The second pass rejects its pipeline bind.
        recorder.RejectPipelineAt(1u);

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_TRUE(result.partial_output);
        // The first pass completed, the second began and was closed without a
        // draw. No further pass is recorded and no target is left open.
        EXPECT_EQ(result.pass_count, 2u);
        EXPECT_EQ(result.draw_count, 1u);
        EXPECT_EQ(recorder.target_begins, 2);
        EXPECT_EQ(recorder.target_ends, 2);
        EXPECT_EQ(recorder.open_targets, 0);
        EXPECT_EQ(recorder.draw_calls, 1);
    }

    TEST(RenderSubmissionExecutionTest, RejectedTargetBeginFlagsPartialOutput)
    {
        RenderSubmission submission = MakeTwoPassSubmission();
        FakeSubmissionFrame frame;
        FakeCommandRecorder recorder;
        recorder.RejectSecondTargetBegin();

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        EXPECT_FALSE(result.succeeded);
        EXPECT_TRUE(result.partial_output);
        EXPECT_EQ(result.pass_count, 1u);
        EXPECT_EQ(result.draw_count, 1u);
        EXPECT_EQ(recorder.open_targets, 0);
    }

    TEST(RenderSubmissionExecutionTest, PresentationPassUsesTheRecorderPresentationBracket)
    {
        RenderSubmission submission{};
        SubmissionPass pass{};
        pass.presentation = true;
        pass.clear_color = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.0f};
        pass.draws.push_back(MakeDraw());
        submission.passes.push_back(std::move(pass));
        FakeSubmissionFrame frame;
        FakeCommandRecorder recorder;

        const RenderSubmissionExecutionResult result =
            RenderSubmissionExecutor::ExecuteOn(submission, frame, recorder);

        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        EXPECT_EQ(recorder.target_begins, 0);
        EXPECT_EQ(recorder.presentation_begins, 1);
        EXPECT_EQ(recorder.open_targets, 0);
    }
}
