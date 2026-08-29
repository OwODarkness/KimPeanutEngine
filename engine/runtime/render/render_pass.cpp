#include "render_pass.h"

#include <array>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        constexpr std::size_t kRenderPassResourceCount = 2;

        constexpr std::size_t ToIndex(RenderPassResource resource) noexcept
        {
            return static_cast<std::size_t>(resource);
        }
    }

    bool RenderPassSchedule::AddPass(RenderPassDeclaration declaration)
    {
        if (declaration.name.empty() || declaration.resources.empty())
        {
            return false;
        }
        passes_.push_back(std::move(declaration));
        return true;
    }

    bool RenderPassSchedule::Validate(std::string &error) const
    {
        error.clear();
        if (passes_.empty())
        {
            error = "A render pass schedule requires at least one pass.";
            return false;
        }
        std::array<bool, kRenderPassResourceCount> has_writer{};

        for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index)
        {
            const RenderPassDeclaration &pass = passes_[pass_index];
            if (pass.name.empty() || pass.resources.empty())
            {
                error = "A render pass requires a name and at least one resource use.";
                return false;
            }
            if (pass.is_terminal && pass_index + 1 != passes_.size())
            {
                error = "A terminal render pass must be last.";
                return false;
            }

            for (const RenderPassResourceUse &use : pass.resources)
            {
                const std::size_t resource_index = ToIndex(use.resource);
                if (resource_index >= has_writer.size())
                {
                    error = "A render pass references an unknown logical resource.";
                    return false;
                }

                if (use.access == RenderPassAccess::Read && !has_writer[resource_index])
                {
                    error = "A render pass reads a resource before any pass writes it.";
                    return false;
                }
                if (use.access == RenderPassAccess::Write)
                {
                    if (has_writer[resource_index])
                    {
                        error = "Render Phase 1 permits one writer per logical resource.";
                        return false;
                    }
                    has_writer[resource_index] = true;
                }
            }
        }
        return true;
    }

    bool RenderPassSchedule::IsValid() const
    {
        std::string error;
        return Validate(error);
    }
}
