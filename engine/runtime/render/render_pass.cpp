#include "render_pass.h"

#include <array>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        constexpr std::size_t kResourceCount =
            static_cast<std::size_t>(RenderPassResource::Count);
        constexpr std::size_t kPassCount =
            static_cast<std::size_t>(FixedRenderPassId::Count);

        constexpr std::size_t ToIndex(RenderPassResource resource) noexcept
        {
            return static_cast<std::size_t>(resource);
        }

        constexpr std::size_t ToIndex(FixedRenderPassId id) noexcept
        {
            return static_cast<std::size_t>(id);
        }

        bool IsValidResource(RenderPassResource resource) noexcept
        {
            return ToIndex(resource) < kResourceCount;
        }

        bool IsValidPassId(FixedRenderPassId id) noexcept
        {
            return ToIndex(id) < kPassCount;
        }

        bool IsValidAccess(RenderPassAccess access) noexcept
        {
            return access == RenderPassAccess::Read || access == RenderPassAccess::Write;
        }

        bool IsValidOwner(RenderPassExecutionOwner owner) noexcept
        {
            return owner == RenderPassExecutionOwner::Renderer ||
                   owner == RenderPassExecutionOwner::External;
        }

        bool IsValidCondition(RenderPassCondition condition) noexcept
        {
            return condition == RenderPassCondition::Always ||
                   condition == RenderPassCondition::DiagnosticCaptureRequested ||
                   condition == RenderPassCondition::ExternalRequest;
        }

        bool IsConditional(RenderPassCondition condition) noexcept
        {
            return condition != RenderPassCondition::Always;
        }

        bool ConditionsAreCompatible(RenderPassCondition writer,
                                     RenderPassCondition reader) noexcept
        {
            return writer == RenderPassCondition::Always || writer == reader;
        }

        bool HasCanonicalPassId(FixedRenderPassId id, std::size_t ordinal) noexcept
        {
            return id == static_cast<FixedRenderPassId>(ordinal);
        }
    }

    FixedRenderPassSequence::FixedRenderPassSequence(FixedRenderPassSequence &&other) noexcept
        : entries_(std::move(other.entries_))
    {
        other.entries_.clear();
    }

    FixedRenderPassSequence &FixedRenderPassSequence::operator=(
        FixedRenderPassSequence &&other) noexcept
    {
        if (this != &other)
        {
            entries_ = std::move(other.entries_);
            other.entries_.clear();
        }
        return *this;
    }

    std::optional<FixedRenderPassSequence> FixedRenderPassSequence::Create(
        std::vector<FixedRenderPassEntry> entries, std::string &error)
    {
        error.clear();
        if (entries.empty())
        {
            error = "A fixed render pass sequence requires at least one pass.";
            return std::nullopt;
        }
        if (entries.size() != kPassCount)
        {
            error = "A fixed render pass sequence must contain every typed pass exactly once.";
            return std::nullopt;
        }

        std::array<bool, kPassCount> seen_ids{};
        std::array<bool, kResourceCount> has_writer{};
        std::array<RenderPassCondition, kResourceCount> writer_conditions{};
        std::array<bool, kResourceCount> has_conditional_writer{};
        std::vector<std::string> names;
        names.reserve(entries.size());

        bool external_seen = false;
        bool terminal_seen = false;
        for (std::size_t pass_index = 0; pass_index < entries.size(); ++pass_index)
        {
            const FixedRenderPassEntry &entry = entries[pass_index];
            if (!IsValidPassId(entry.id))
            {
                error = "A fixed render pass has an unknown typed ID.";
                return std::nullopt;
            }
            if (!HasCanonicalPassId(entry.id, pass_index))
            {
                error = "A fixed render pass typed ID does not match its canonical ordinal.";
                return std::nullopt;
            }
            const std::size_t pass_id = ToIndex(entry.id);
            if (seen_ids[pass_id])
            {
                error = "A fixed render pass typed ID appears more than once.";
                return std::nullopt;
            }
            seen_ids[pass_id] = true;
            if (entry.name.empty() || entry.resources.empty())
            {
                error = "A fixed render pass requires a name and resource use.";
                return std::nullopt;
            }
            for (const std::string &name : names)
            {
                if (name == entry.name)
                {
                    error = "A fixed render pass name appears more than once.";
                    return std::nullopt;
                }
            }
            names.push_back(entry.name);
            if (!IsValidOwner(entry.owner) || !IsValidCondition(entry.condition))
            {
                error = "A fixed render pass has an unknown owner or condition.";
                return std::nullopt;
            }
            if (entry.owner == RenderPassExecutionOwner::External)
            {
                if (external_seen || !entry.terminal || pass_index + 1 != entries.size() ||
                    entry.condition != RenderPassCondition::ExternalRequest)
                {
                    error = "The external fixed render pass must be the sole terminal entry.";
                    return std::nullopt;
                }
                external_seen = true;
            }
            else if (entry.condition == RenderPassCondition::ExternalRequest)
            {
                error = "Only an external fixed render pass may use ExternalRequest.";
                return std::nullopt;
            }
            if (entry.terminal)
            {
                if (terminal_seen || pass_index + 1 != entries.size())
                {
                    error = "A terminal fixed render pass must be the last entry.";
                    return std::nullopt;
                }
                terminal_seen = true;
            }

            std::array<bool, kResourceCount> used_resources{};
            for (const RenderPassResourceUse &use : entry.resources)
            {
                if (!IsValidResource(use.resource) || !IsValidAccess(use.access))
                {
                    error = "A fixed render pass has an unknown resource or access value.";
                    return std::nullopt;
                }
                const std::size_t resource = ToIndex(use.resource);
                if (used_resources[resource])
                {
                    error = "A fixed render pass uses one logical resource more than once.";
                    return std::nullopt;
                }
                used_resources[resource] = true;
                if (entry.owner == RenderPassExecutionOwner::External &&
                    use.access == RenderPassAccess::Write)
                {
                    error = "An external terminal pass cannot write a renderer resource.";
                    return std::nullopt;
                }
                if (use.access == RenderPassAccess::Read)
                {
                    if (!has_writer[resource])
                    {
                        error = "A fixed render pass reads a resource before its writer.";
                        return std::nullopt;
                    }
                    if (!ConditionsAreCompatible(writer_conditions[resource], entry.condition))
                    {
                        error = "A conditional reader depends on an incompatible conditional writer.";
                        return std::nullopt;
                    }
                    if (entry.condition == RenderPassCondition::Always &&
                        has_conditional_writer[resource])
                    {
                        error = "A required reader depends on a conditional writer.";
                        return std::nullopt;
                    }
                }
                else
                {
                    if (has_writer[resource])
                    {
                        error = "A fixed sequence permits one writer per logical resource.";
                        return std::nullopt;
                    }
                    has_writer[resource] = true;
                    writer_conditions[resource] = entry.condition;
                    has_conditional_writer[resource] = IsConditional(entry.condition);
                }
            }
        }

        for (const bool seen : seen_ids)
        {
            if (!seen)
            {
                error = "A fixed render pass typed ID is missing from the sequence.";
                return std::nullopt;
            }
        }
        if (!terminal_seen || !external_seen)
        {
            error = "A fixed render pass sequence requires one external terminal entry.";
            return std::nullopt;
        }
        return FixedRenderPassSequence(std::move(entries));
    }

    FixedRenderPassFrame::FixedRenderPassFrame(const FixedRenderPassSequence &sequence,
                                               bool diagnostic_capture_requested)
        : sequence_(&sequence), diagnostic_capture_requested_(diagnostic_capture_requested)
    {
        outcomes_.fill(RenderPassOutcome::Pending);
    }

    FixedRenderPassFrame::FixedRenderPassFrame(FixedRenderPassFrame &&other) noexcept
        : sequence_(other.sequence_), outcomes_(other.outcomes_),
          diagnostic_capture_requested_(other.diagnostic_capture_requested_),
          renderer_executed_(other.renderer_executed_),
          external_executed_(other.external_executed_), finalized_(other.finalized_),
          required_failure_(other.required_failure_), cursor_(other.cursor_)
    {
        other.sequence_ = nullptr;
        other.outcomes_.fill(RenderPassOutcome::Pending);
        other.diagnostic_capture_requested_ = false;
        other.renderer_executed_ = false;
        other.external_executed_ = false;
        other.finalized_ = false;
        other.required_failure_ = false;
        other.cursor_ = 0;
    }

    FixedRenderPassFrame &FixedRenderPassFrame::operator=(FixedRenderPassFrame &&other) noexcept
    {
        if (this != &other)
        {
            sequence_ = other.sequence_;
            outcomes_ = other.outcomes_;
            diagnostic_capture_requested_ = other.diagnostic_capture_requested_;
            renderer_executed_ = other.renderer_executed_;
            external_executed_ = other.external_executed_;
            finalized_ = other.finalized_;
            required_failure_ = other.required_failure_;
            cursor_ = other.cursor_;

            other.sequence_ = nullptr;
            other.outcomes_.fill(RenderPassOutcome::Pending);
            other.diagnostic_capture_requested_ = false;
            other.renderer_executed_ = false;
            other.external_executed_ = false;
            other.finalized_ = false;
            other.required_failure_ = false;
            other.cursor_ = 0;
        }
        return *this;
    }

    bool FixedRenderPassFrame::IsValidId(FixedRenderPassId id) const noexcept
    {
        return id != FixedRenderPassId::Count &&
               ToIndex(id) < static_cast<std::size_t>(FixedRenderPassId::Count);
    }

    RenderPassOutcome FixedRenderPassFrame::GetOutcome(FixedRenderPassId id) const noexcept
    {
        if (!IsValidId(id))
        {
            return RenderPassOutcome::Pending;
        }
        return outcomes_[ToIndex(id)];
    }

    bool FixedRenderPassFrame::ExecuteRenderer(
        const std::function<bool(FixedRenderPassId)> &executor)
    {
        if (!sequence_ || finalized_ || renderer_executed_ || !executor)
        {
            return false;
        }
        const auto &entries = sequence_->Entries();
        for (; cursor_ < entries.size(); ++cursor_)
        {
            const FixedRenderPassEntry &entry = entries[cursor_];
            if (entry.owner == RenderPassExecutionOwner::External)
            {
                break;
            }
            if (entry.condition == RenderPassCondition::DiagnosticCaptureRequested &&
                !diagnostic_capture_requested_)
            {
                outcomes_[ToIndex(entry.id)] = RenderPassOutcome::SkippedCondition;
                continue;
            }
            const bool succeeded = executor(entry.id);
            outcomes_[ToIndex(entry.id)] = succeeded ? RenderPassOutcome::Executed
                                                     : RenderPassOutcome::Failed;
            if (!succeeded && entry.condition == RenderPassCondition::Always)
            {
                required_failure_ = true;
            }
        }
        renderer_executed_ = true;
        return true;
    }

    bool FixedRenderPassFrame::ExecuteExternal(const std::function<void()> &executor)
    {
        if (!sequence_ || finalized_ || !renderer_executed_ || external_executed_ || !executor ||
            cursor_ >= sequence_->Entries().size())
        {
            return false;
        }
        const FixedRenderPassEntry &entry = sequence_->Entries()[cursor_];
        if (entry.owner != RenderPassExecutionOwner::External || !entry.terminal ||
            entry.condition != RenderPassCondition::ExternalRequest)
        {
            return false;
        }
        executor();
        outcomes_[ToIndex(entry.id)] = RenderPassOutcome::Executed;
        external_executed_ = true;
        ++cursor_;
        return true;
    }

    bool FixedRenderPassFrame::Finalize(std::string &error)
    {
        error.clear();
        if (finalized_)
        {
            return true;
        }
        if (!renderer_executed_ || !sequence_ ||
            (!external_executed_ && cursor_ >= sequence_->Entries().size()))
        {
            error = "A fixed render pass frame cannot finalize before renderer execution.";
            return false;
        }
        if (!external_executed_ &&
            (cursor_ >= sequence_->Entries().size() ||
             sequence_->Entries()[cursor_].owner != RenderPassExecutionOwner::External ||
             !sequence_->Entries()[cursor_].terminal))
        {
            error = "A fixed render pass frame did not reach its external terminal entry.";
            return false;
        }
        if (!external_executed_)
        {
            const FixedRenderPassEntry &external_entry = sequence_->Entries()[cursor_];
            outcomes_[ToIndex(external_entry.id)] = RenderPassOutcome::SkippedExternal;
            ++cursor_;
        }
        for (const FixedRenderPassEntry &entry : sequence_->Entries())
        {
            const RenderPassOutcome outcome = outcomes_[ToIndex(entry.id)];
            if (entry.owner == RenderPassExecutionOwner::Renderer &&
                entry.condition == RenderPassCondition::Always &&
                outcome != RenderPassOutcome::Executed &&
                outcome != RenderPassOutcome::Failed)
            {
                error = "A required fixed render pass was not visited.";
                return false;
            }
        }
        finalized_ = true;
        return true;
    }
}
