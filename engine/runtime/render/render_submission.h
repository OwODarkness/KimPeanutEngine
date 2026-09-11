#ifndef KPENGINE_RUNTIME_RENDER_SUBMISSION_H
#define KPENGINE_RUNTIME_RENDER_SUBMISSION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_target.h"
#include "graphics/backend/common/resource_binding.h"

namespace kpengine::render
{
    struct SubmissionBufferWrite final
    {
        graphics::BufferHandle destination;
        std::size_t offset = 0u;
        std::vector<std::byte> bytes;
    };

    struct SubmissionUniformData final
    {
        std::uint32_t set = 0u;
        std::uint32_t binding = 0u;
        std::vector<std::byte> bytes;
    };

    struct SubmissionSampledTexture final
    {
        std::uint32_t set = 0u;
        std::uint32_t binding = 0u;
        graphics::TextureHandle texture;
        graphics::SamplerHandle sampler;
    };

    struct SubmissionDraw final
    {
        graphics::PipelineHandle pipeline;
        graphics::GeometryView geometry;
        std::vector<SubmissionUniformData> uniforms;
        std::vector<SubmissionSampledTexture> textures;
        graphics::Viewport viewport;
        std::optional<graphics::Scissor> scissor;
        std::uint32_t index_count = 0u;
        std::uint32_t first_index = 0u;
        std::int32_t vertex_offset = 0;
    };

    struct SubmissionPass final
    {
        graphics::RenderTargetHandle target;
        // A presentation pass targets the backend-owned swapchain instead of
        // a Render-owned offscreen target. `target` remains unused for this
        // pass and may be invalid.
        bool presentation = false;
        // Optional display-space clear for a presentation pass. Offscreen
        // targets continue to use their attachment descriptor clear value.
        std::optional<std::array<float, 4>> clear_color;
        std::vector<SubmissionDraw> draws;
    };

    struct RenderSubmission final
    {
        std::vector<SubmissionBufferWrite> buffer_writes;
        std::vector<SubmissionPass> passes;
    };

    struct RenderSubmissionValidation final
    {
        bool valid = false;
        std::string diagnostic;
        std::size_t draw_count = 0u;
        std::size_t upload_bytes = 0u;
        std::size_t uniform_bytes = 0u;
    };

    RenderSubmissionValidation ValidateRenderSubmission(
        const RenderSubmission &submission);
}

#endif
