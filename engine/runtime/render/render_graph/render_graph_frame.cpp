#include "render_graph_frame.h"

#include <utility>

namespace kpengine::render
{
    RenderGraphFrame::RenderGraphFrame(const CompiledRenderGraph &graph)
        : graph_(&graph), outcomes_(graph.Passes().size(), RenderGraphPassOutcome::Pending)
    {
    }

    RenderGraphFrame::RenderGraphFrame(RenderGraphFrame &&other) noexcept
        : graph_(other.graph_), outcomes_(std::move(other.outcomes_)), cursor_(other.cursor_),
          renderer_executed_(other.renderer_executed_),
          external_executed_(other.external_executed_), finalized_(other.finalized_),
          required_failure_(other.required_failure_)
    {
        other.graph_ = nullptr;
        other.outcomes_.clear();
        other.cursor_ = 0;
        other.renderer_executed_ = false;
        other.external_executed_ = false;
        other.finalized_ = false;
        other.required_failure_ = false;
    }

    RenderGraphFrame &RenderGraphFrame::operator=(RenderGraphFrame &&other) noexcept
    {
        if (this != &other)
        {
            graph_ = other.graph_;
            outcomes_ = std::move(other.outcomes_);
            cursor_ = other.cursor_;
            renderer_executed_ = other.renderer_executed_;
            external_executed_ = other.external_executed_;
            finalized_ = other.finalized_;
            required_failure_ = other.required_failure_;

            other.graph_ = nullptr;
            other.outcomes_.clear();
            other.cursor_ = 0;
            other.renderer_executed_ = false;
            other.external_executed_ = false;
            other.finalized_ = false;
            other.required_failure_ = false;
        }
        return *this;
    }

    bool RenderGraphFrame::ExecuteRenderer(
        const std::function<bool(const CompiledRenderGraph::Pass &)> &executor)
    {
        if (!graph_ || finalized_ || renderer_executed_ || !executor)
        {
            return false;
        }
        // A failure does not stop the sweep: the caller keeps its existing
        // continue-and-report behavior and reads HasRequiredFailure().
        const std::vector<CompiledRenderGraph::Pass> &passes = graph_->Passes();
        while (cursor_ < passes.size())
        {
            const CompiledRenderGraph::Pass &pass = passes[cursor_];
            if (pass.owner == RenderGraphPassOwner::External)
            {
                break;
            }
            const bool succeeded = executor(pass);
            outcomes_[cursor_] =
                succeeded ? RenderGraphPassOutcome::Executed : RenderGraphPassOutcome::Failed;
            if (!succeeded && pass.condition == RenderGraphPassCondition::Always)
            {
                required_failure_ = true;
            }
            ++cursor_;
        }
        renderer_executed_ = true;
        return true;
    }

    bool RenderGraphFrame::ExecuteExternal(const std::function<void()> &executor)
    {
        if (!graph_ || finalized_ || !renderer_executed_ || external_executed_ || !executor)
        {
            return false;
        }
        const std::vector<CompiledRenderGraph::Pass> &passes = graph_->Passes();
        if (cursor_ >= passes.size() || passes[cursor_].owner != RenderGraphPassOwner::External ||
            !passes[cursor_].terminal)
        {
            return false;
        }
        executor();
        outcomes_[cursor_] = RenderGraphPassOutcome::Executed;
        external_executed_ = true;
        ++cursor_;
        return true;
    }

    bool RenderGraphFrame::Finalize(std::string &error)
    {
        error.clear();
        if (finalized_)
        {
            return true;
        }
        if (!graph_ || !renderer_executed_)
        {
            error = "A render graph frame cannot finalize before renderer execution.";
            return false;
        }
        const std::vector<CompiledRenderGraph::Pass> &passes = graph_->Passes();
        if (!external_executed_ && cursor_ < passes.size())
        {
            const CompiledRenderGraph::Pass &pass = passes[cursor_];
            if (pass.owner != RenderGraphPassOwner::External || !pass.terminal)
            {
                error = "A render graph frame did not reach its external terminal pass.";
                return false;
            }
            // An unrequested terminal is a normal outcome, not a failure.
            outcomes_[cursor_] = RenderGraphPassOutcome::SkippedExternal;
            ++cursor_;
        }
        if (cursor_ != passes.size())
        {
            error = "A render graph pass follows the external terminal pass.";
            return false;
        }
        for (std::size_t index = 0; index < passes.size(); ++index)
        {
            if (passes[index].owner != RenderGraphPassOwner::Renderer ||
                passes[index].condition != RenderGraphPassCondition::Always)
            {
                continue;
            }
            const RenderGraphPassOutcome outcome = outcomes_[index];
            if (outcome != RenderGraphPassOutcome::Executed &&
                outcome != RenderGraphPassOutcome::Failed)
            {
                error = "A required render graph pass was not visited.";
                return false;
            }
        }
        finalized_ = true;
        return true;
    }

    RenderGraphPassOutcome RenderGraphFrame::GetOutcome(uint64_t user_key) const noexcept
    {
        if (!graph_)
        {
            return RenderGraphPassOutcome::NotInPlan;
        }
        const std::vector<CompiledRenderGraph::Pass> &passes = graph_->Passes();
        for (std::size_t index = 0; index < passes.size(); ++index)
        {
            if (passes[index].user_key.has_value() && *passes[index].user_key == user_key)
            {
                return outcomes_[index];
            }
        }
        return RenderGraphPassOutcome::NotInPlan;
    }
}
