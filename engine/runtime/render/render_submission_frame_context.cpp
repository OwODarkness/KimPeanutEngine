#include "render_submission_executor.h"

#include "frame_context.h"

namespace kpengine::render
{
    RenderSubmissionExecutionResult RenderSubmissionExecutor::Execute(
        const RenderSubmission &submission, FrameContext &frame,
        graphics::CommandRecorder &recorder)
    {
        return ExecuteOn(submission, frame, recorder);
    }
}
