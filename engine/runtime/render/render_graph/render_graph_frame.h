#ifndef KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_RENDER_GRAPH_FRAME_H
#define KPENGINE_RUNTIME_RENDER_RENDER_GRAPH_RENDER_GRAPH_FRAME_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "render_graph.h"

namespace kpengine::render
{
    enum class RenderGraphPassOutcome : uint8_t
    {
        // The plan compiled for this frame's condition set has no pass with the
        // queried key, which is how a culled conditional pass reports itself.
        NotInPlan,
        Pending,
        Executed,
        SkippedExternal,
        Failed,
    };

    // Executes one compiled plan for one frame. It borrows the compiled graph,
    // which must outlive it, and owns nothing but the frame's own execution
    // state. This is the renderer's pass scheduler.
    class RenderGraphFrame final
    {
    public:
        explicit RenderGraphFrame(const CompiledRenderGraph &graph);
        ~RenderGraphFrame() = default;
        RenderGraphFrame(const RenderGraphFrame &) = delete;
        RenderGraphFrame &operator=(const RenderGraphFrame &) = delete;
        RenderGraphFrame(RenderGraphFrame &&other) noexcept;
        RenderGraphFrame &operator=(RenderGraphFrame &&other) noexcept;

        // Visits renderer-owned passes in compiled order and stops at the
        // external terminal, which the caller runs through ExecuteExternal.
        bool ExecuteRenderer(
            const std::function<bool(const CompiledRenderGraph::Pass &)> &executor);
        bool ExecuteExternal(const std::function<void()> &executor);
        bool Finalize(std::string &error);

        RenderGraphPassOutcome GetOutcome(uint64_t user_key) const noexcept;
        bool HasRequiredFailure() const noexcept { return required_failure_; }
        bool IsFinalized() const noexcept { return finalized_; }

    private:
        const CompiledRenderGraph *graph_ = nullptr;
        std::vector<RenderGraphPassOutcome> outcomes_;
        std::size_t cursor_ = 0;
        bool renderer_executed_ = false;
        bool external_executed_ = false;
        bool finalized_ = false;
        bool required_failure_ = false;
    };
}

#endif
