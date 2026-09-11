#ifndef KPENGINE_RUNTIME_RENDER_SUBMISSION_EXECUTOR_H
#define KPENGINE_RUNTIME_RENDER_SUBMISSION_EXECUTOR_H

#include <cstddef>
#include <string>

#include "render_submission.h"
#include "render_submission_frame.h"

namespace kpengine::render
{
    class FrameContext;

    struct RenderSubmissionExecutionResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        std::size_t pass_count = 0u;
        std::size_t draw_count = 0u;
        std::size_t upload_bytes = 0u;
        std::size_t uniform_bytes = 0u;
        std::size_t binding_count = 0u;
        // Set when a pass had already begun before a command was rejected, so
        // the caller knows the frame contains a closed but incomplete result.
        bool partial_output = false;
    };

    class RenderSubmissionExecutor final
    {
    public:
        static RenderSubmissionExecutionResult Execute(
            const RenderSubmission &submission, FrameContext &frame,
            graphics::CommandRecorder &recorder);

        // Production-free entry point. Used directly by the contract tests and
        // by any caller that owns a non-FrameContext frame allocator.
        static RenderSubmissionExecutionResult ExecuteOn(
            const RenderSubmission &submission, RenderSubmissionFrame &frame,
            graphics::CommandRecorder &recorder);
    };
}

#endif
