#include "submitted_transform_history.h"

namespace kpengine::render
{
    uint64_t SubmittedTransformHistory::MakeKey(RenderableHandle handle) noexcept
    {
        return (static_cast<uint64_t>(handle.id) << 16u) | handle.generation;
    }

    void SubmittedTransformHistory::BeginFrame(const std::vector<MeshProxy> &snapshot)
    {
        pending_.clear();
        pending_.reserve(snapshot.size());
        for (const MeshProxy &proxy : snapshot)
        {
            if (proxy.handle.IsValid())
            {
                pending_.insert_or_assign(MakeKey(proxy.handle), proxy.world_transform);
            }
        }
    }

    void SubmittedTransformHistory::CommitFrame(bool accepted)
    {
        if (accepted)
        {
            submitted_.swap(pending_);
        }
        pending_.clear();
    }

    void SubmittedTransformHistory::Reset() noexcept
    {
        submitted_.clear();
        pending_.clear();
    }

    std::optional<Transform3f> SubmittedTransformHistory::FindPrevious(
        RenderableHandle handle) const
    {
        const auto previous = submitted_.find(MakeKey(handle));
        return previous == submitted_.end()
                   ? std::nullopt
                   : std::optional<Transform3f>{previous->second};
    }
}
