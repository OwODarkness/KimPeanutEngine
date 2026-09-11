#include "render_submission.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace kpengine::render
{
    namespace
    {
        bool IsFinite(const float value) noexcept
        {
            return std::isfinite(value) != 0;
        }

        bool AddWillOverflow(const std::size_t left, const std::size_t right) noexcept
        {
            return left > std::numeric_limits<std::size_t>::max() - right;
        }

        bool ValidateViewport(const graphics::Viewport &viewport,
                              std::string &diagnostic)
        {
            if (!IsFinite(viewport.x) || !IsFinite(viewport.y) ||
                !IsFinite(viewport.width) || !IsFinite(viewport.height) ||
                !IsFinite(viewport.min_depth) || !IsFinite(viewport.max_depth) ||
                viewport.width <= 0.0f || viewport.height <= 0.0f ||
                viewport.min_depth < 0.0f || viewport.max_depth > 1.0f ||
                viewport.min_depth > viewport.max_depth)
            {
                diagnostic = "render submission viewport is invalid";
                return false;
            }
            return true;
        }

        bool ValidateBindings(const SubmissionDraw &draw, std::string &diagnostic,
                              std::size_t &uniform_bytes)
        {
            std::unordered_set<std::uint64_t> bindings;
            const auto add_binding = [&bindings, &diagnostic](const std::uint32_t set,
                                                               const std::uint32_t binding)
            {
                if (set != 0u)
                {
                    diagnostic = "render submission currently supports descriptor set zero only";
                    return false;
                }
                const std::uint64_t key = (static_cast<std::uint64_t>(set) << 32u) | binding;
                if (!bindings.insert(key).second)
                {
                    diagnostic = "render submission contains duplicate descriptor binding";
                    return false;
                }
                return true;
            };

            for (const SubmissionUniformData &uniform : draw.uniforms)
            {
                if (uniform.bytes.empty())
                {
                    diagnostic = "render submission contains an empty uniform range";
                    return false;
                }
                if (AddWillOverflow(uniform_bytes, uniform.bytes.size()))
                {
                    diagnostic = "render submission uniform byte count overflows";
                    return false;
                }
                uniform_bytes += uniform.bytes.size();
                if (!add_binding(uniform.set, uniform.binding))
                {
                    return false;
                }
            }

            for (const SubmissionSampledTexture &texture : draw.textures)
            {
                if (!texture.texture.IsValid() || !texture.sampler.IsValid())
                {
                    diagnostic = "render submission contains an invalid sampled texture";
                    return false;
                }
                if (!add_binding(texture.set, texture.binding))
                {
                    return false;
                }
            }
            return true;
        }
    }

    RenderSubmissionValidation ValidateRenderSubmission(
        const RenderSubmission &submission)
    {
        RenderSubmissionValidation result{};
        for (const SubmissionBufferWrite &write : submission.buffer_writes)
        {
            if (!write.destination.IsValid() || write.bytes.empty())
            {
                result.diagnostic = "render submission contains an invalid buffer write";
                return result;
            }
            if (AddWillOverflow(write.offset, write.bytes.size()))
            {
                result.diagnostic = "render submission buffer write range overflows";
                return result;
            }
            if (AddWillOverflow(result.upload_bytes, write.bytes.size()))
            {
                result.diagnostic = "render submission upload byte count overflows";
                return result;
            }
            result.upload_bytes += write.bytes.size();
        }

        for (const SubmissionPass &pass : submission.passes)
        {
            if ((!pass.target.IsValid() && !pass.presentation) ||
                (pass.presentation && pass.target.IsValid()))
            {
                result.diagnostic = pass.presentation
                                        ? "presentation pass must not carry an offscreen target"
                                        : "render submission contains an invalid render target";
                return result;
            }
            for (const SubmissionDraw &draw : pass.draws)
            {
                if (!draw.pipeline.IsValid() || !draw.geometry.indices.buffer.IsValid() ||
                    draw.geometry.vertices.empty() || draw.index_count == 0u)
                {
                    result.diagnostic = "render submission contains an invalid draw";
                    return result;
                }
                if (draw.first_index > std::numeric_limits<std::uint32_t>::max() -
                                     draw.index_count)
                {
                    result.diagnostic = "render submission index range overflows";
                    return result;
                }
                for (const graphics::VertexBufferView &vertex : draw.geometry.vertices)
                {
                    if (!vertex.buffer.IsValid())
                    {
                        result.diagnostic = "render submission contains an invalid vertex buffer";
                        return result;
                    }
                }
                if (draw.geometry.indices.type != graphics::IndexElementType::UInt16 &&
                    draw.geometry.indices.type != graphics::IndexElementType::UInt32)
                {
                    result.diagnostic = "render submission contains an invalid index type";
                    return result;
                }
                if (!ValidateViewport(draw.viewport, result.diagnostic))
                {
                    return result;
                }
                if (draw.scissor.has_value())
                {
                    const graphics::Scissor &scissor = *draw.scissor;
                    if (scissor.width == 0u || scissor.height == 0u || scissor.x < 0 ||
                        scissor.y < 0)
                    {
                        result.diagnostic = "render submission scissor is invalid";
                        return result;
                    }
                }
                if (!ValidateBindings(draw, result.diagnostic, result.uniform_bytes))
                {
                    return result;
                }
                ++result.draw_count;
            }
        }

        result.valid = true;
        return result;
    }
}
