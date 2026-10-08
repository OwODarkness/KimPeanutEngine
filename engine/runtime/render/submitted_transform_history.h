#ifndef KPENGINE_RUNTIME_RENDER_SUBMITTED_TRANSFORM_HISTORY_H
#define KPENGINE_RUNTIME_RENDER_SUBMITTED_TRANSFORM_HISTORY_H

#include <optional>
#include <unordered_map>
#include <vector>

#include "render/render_world/mesh_proxy.h"

namespace kpengine::render
{
    // CPU-side history is a value snapshot. It advances only after the frame
    // graph has finalized successfully; a rejected frame remains unsubmitted.
    class SubmittedTransformHistory final
    {
    public:
        void BeginFrame(const std::vector<MeshProxy> &snapshot);
        void CommitFrame(bool accepted);
        void Reset() noexcept;
        std::optional<Transform3f> FindPrevious(RenderableHandle handle) const;

    private:
        static uint64_t MakeKey(RenderableHandle handle) noexcept;

        std::unordered_map<uint64_t, Transform3f> submitted_;
        std::unordered_map<uint64_t, Transform3f> pending_;
    };
}

#endif
