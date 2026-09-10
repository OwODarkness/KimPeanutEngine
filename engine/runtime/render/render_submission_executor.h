#ifndef KPENGINE_RUNTIME_RENDER_SUBMISSION_EXECUTOR_H
#define KPENGINE_RUNTIME_RENDER_SUBMISSION_EXECUTOR_H

#include <cstddef>
#include <string>

#include "render_submission.h"

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
    };

    class RenderSubmissionExecutor final
    {
    public:
        static RenderSubmissionExecutionResult Execute(
            const RenderSubmission &submission, FrameContext &frame,
            graphics::CommandRecorder &recorder);
    };
}

#endif
