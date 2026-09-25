#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_HISTORY_PROGRESS_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_HISTORY_PROGRESS_H

#include <cstdint>

namespace kpengine::render::detail
{
    struct PathTraceHistoryProgress
    {
        uint32_t sample_count = 0;
        uint32_t write_index = 0;

        friend bool operator==(const PathTraceHistoryProgress &,
                               const PathTraceHistoryProgress &) = default;
    };

    constexpr PathTraceHistoryProgress CommitPathTraceHistoryProgress(
        PathTraceHistoryProgress current, bool finalized, bool frame_execution_failed,
        bool required_pass_failed, bool path_trace_active,
        uint32_t samples_per_dispatch) noexcept
    {
        if (!finalized || frame_execution_failed || required_pass_failed ||
            !path_trace_active)
        {
            return current;
        }

        current.sample_count += samples_per_dispatch;
        current.write_index ^= 1u;
        return current;
    }
}

#endif
