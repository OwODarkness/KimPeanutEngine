#include "render_target_validation.h"

namespace kpengine::graphics
{
    namespace
    {
        bool Fail(std::string *error, const char *message)
        {
            if (error)
            {
                *error = message;
            }
            return false;
        }

        bool IsPowerOfTwoSampleCount(uint32_t sample_count)
        {
            return sample_count >= 1 && sample_count <= 64 &&
                   (sample_count & (sample_count - 1)) == 0;
        }
    }

    bool ValidateRenderTargetDesc(const RenderTargetDesc &desc, std::string *error)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return Fail(error, "render target requires a non-zero extent");
        }
        if (!IsPowerOfTwoSampleCount(desc.sample_count))
        {
            return Fail(error, "render target sample count must be a power of two in [1, 64]");
        }
        // D2 has no multisample image/resolve contract yet. Accepting a
        // multisample count here would allocate single-sample attachments and
        // make ordinary sampled-texture bindings invalid.
        if (desc.sample_count != 1)
        {
            return Fail(error, "multisample render targets are not supported until resolve is implemented");
        }
        if (desc.color_attachments.empty() && !desc.depth.has_value())
        {
            return Fail(error, "render target requires at least one color or depth attachment");
        }
        for (const RenderTargetColorAttachment &attachment : desc.color_attachments)
        {
            if (attachment.format == TextureFormat::TEXTURE_FORMAT_UNKNOW)
            {
                return Fail(error, "render target color attachment format is unknown");
            }
        }
        if (desc.depth.has_value() &&
            desc.depth->format == TextureFormat::TEXTURE_FORMAT_UNKNOW)
        {
            return Fail(error, "render target depth attachment format is unknown");
        }
        return true;
    }

    bool ValidateRenderTargetPipelineCompatibility(const RenderTargetDesc &target,
                                                   const PipelineDesc &pipeline,
                                                   std::string *error)
    {
        if (pipeline.color_attachment_formats.size() != target.color_attachments.size())
        {
            return Fail(error, "pipeline and render target color attachment counts differ");
        }
        for (size_t index = 0; index < pipeline.color_attachment_formats.size(); ++index)
        {
            if (pipeline.color_attachment_formats[index] !=
                target.color_attachments[index].format)
            {
                return Fail(error, "pipeline and render target color attachment formats differ");
            }
        }
        if (pipeline.depth_attachment_format != TextureFormat::TEXTURE_FORMAT_UNKNOW)
        {
            if (!target.depth.has_value())
            {
                return Fail(error, "pipeline requires depth but the render target has none");
            }
            if (pipeline.depth_attachment_format != target.depth->format)
            {
                return Fail(error, "pipeline and render target depth attachment formats differ");
            }
        }
        if (pipeline.multisample_state.rasterization_samples != target.sample_count)
        {
            return Fail(error, "pipeline and render target sample counts differ");
        }
        return true;
    }
}
