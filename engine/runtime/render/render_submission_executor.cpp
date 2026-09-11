#include "render_submission_executor.h"

#include <cstring>

namespace kpengine::render
{
    namespace
    {
        struct PreparedDraw final
        {
            graphics::DescriptorSetHandle bindings;
        };
    }

    RenderSubmissionExecutionResult RenderSubmissionExecutor::ExecuteOn(
        const RenderSubmission &submission, RenderSubmissionFrame &frame,
        graphics::CommandRecorder &recorder)
    {
        RenderSubmissionExecutionResult result{};
        const RenderSubmissionValidation validation = ValidateRenderSubmission(submission);
        if (!validation.valid)
        {
            result.diagnostic = validation.diagnostic;
            return result;
        }
        if (!frame.IsActive())
        {
            result.diagnostic = "render submission requires an active frame context";
            return result;
        }

        std::vector<std::vector<PreparedDraw>> prepared;
        prepared.reserve(submission.passes.size());
        for (const SubmissionPass &pass : submission.passes)
        {
            std::vector<PreparedDraw> prepared_pass;
            prepared_pass.reserve(pass.draws.size());
            for (const SubmissionDraw &draw : pass.draws)
            {
                graphics::ResourceBindingSetDesc descriptor{};
                bool has_set = false;
                for (const SubmissionUniformData &uniform : draw.uniforms)
                {
                    const UniformAllocation allocation = frame.AllocateUniform(uniform.bytes.size());
                    if (!allocation.IsValid())
                    {
                        result.diagnostic = "render submission uniform allocation failed";
                        return result;
                    }
                    std::memcpy(allocation.mapped, uniform.bytes.data(), uniform.bytes.size());
                    descriptor.set = uniform.set;
                    has_set = true;
                    descriptor.bindings.push_back(graphics::UniformBufferBinding{
                        uniform.set, uniform.binding, allocation.buffer,
                        allocation.offset, allocation.range});
                    result.uniform_bytes += uniform.bytes.size();
                }
                for (const SubmissionSampledTexture &texture : draw.textures)
                {
                    descriptor.set = texture.set;
                    has_set = true;
                    descriptor.bindings.push_back(graphics::SampledTextureBinding{
                        texture.set, texture.binding, texture.texture, texture.sampler});
                }

                graphics::DescriptorSetHandle bindings;
                if (has_set)
                {
                    bindings = frame.AllocateResourceBindingSet(draw.pipeline, descriptor);
                    if (!bindings.IsValid())
                    {
                        result.diagnostic = "render submission resource binding allocation failed";
                        return result;
                    }
                    ++result.binding_count;
                }
                prepared_pass.push_back({bindings});
            }
            prepared.push_back(std::move(prepared_pass));
        }

        for (const SubmissionBufferWrite &write : submission.buffer_writes)
        {
            if (!frame.WriteFrameBuffer(write.destination, write.offset,
                                        write.bytes.data(), write.bytes.size()))
            {
                result.diagnostic = "render submission buffer write failed";
                return result;
            }
            result.upload_bytes += write.bytes.size();
        }

        for (std::size_t pass_index = 0u; pass_index < submission.passes.size(); ++pass_index)
        {
            const SubmissionPass &pass = submission.passes[pass_index];
            const bool began = pass.presentation ? recorder.BeginPresentation(
                                                       pass.clear_color.has_value()
                                                           ? &pass.clear_color.value()
                                                           : nullptr)
                                                 : recorder.BeginRenderTarget(pass.target);
            if (!began)
            {
                result.diagnostic = "render submission render target begin failed";
                result.partial_output = result.pass_count != 0u;
                return result;
            }
            bool pass_succeeded = true;
            for (std::size_t draw_index = 0u; draw_index < pass.draws.size(); ++draw_index)
            {
                const SubmissionDraw &draw = pass.draws[draw_index];
                if (!recorder.BindPipeline(draw.pipeline) ||
                    !recorder.BindGeometry(draw.geometry))
                {
                    result.diagnostic = "render submission command binding failed";
                    pass_succeeded = false;
                    break;
                }
                const graphics::DescriptorSetHandle bindings =
                    prepared[pass_index][draw_index].bindings;
                if (bindings.IsValid() &&
                    !recorder.BindResourceBindings(draw.pipeline, bindings))
                {
                    result.diagnostic = "render submission resource binding command failed";
                    pass_succeeded = false;
                    break;
                }
                recorder.SetViewport(draw.viewport);
                if (draw.scissor.has_value())
                {
                    recorder.SetScissor(*draw.scissor);
                }
                recorder.DrawIndexed(draw.index_count, 1u, draw.first_index,
                                     draw.vertex_offset, 0u);
                ++result.draw_count;
            }
            // Every pass that began is closed, including one that rejected a
            // command, so the recorder never leaves a target open.
            recorder.EndRenderTarget();
            ++result.pass_count;
            if (!pass_succeeded)
            {
                // The frame now holds a closed but incomplete result. The
                // caller decides whether to present it; this executor never
                // does.
                result.partial_output = true;
                return result;
            }
        }

        result.succeeded = true;
        return result;
    }
}
