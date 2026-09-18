#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_H

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "api.h"
#include "texture.h"

namespace kpengine::graphics
{
    enum class RenderTargetLoadOp : uint8_t
    {
        Clear,
        Load,
        DontCare
    };

    enum class RenderTargetStoreOp : uint8_t
    {
        Store,
        DontCare
    };

    struct RenderTargetColorAttachment
    {
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        RenderTargetLoadOp load_op = RenderTargetLoadOp::Clear;
        RenderTargetStoreOp store_op = RenderTargetStoreOp::Store;
        std::array<float, 4> clear_color{0.f, 0.f, 0.f, 1.f};
    };

    struct RenderTargetDepthAttachment
    {
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_D32;
        RenderTargetLoadOp load_op = RenderTargetLoadOp::Clear;
        RenderTargetStoreOp store_op = RenderTargetStoreOp::Store;
        float clear_depth = 1.0f;
        uint32_t clear_stencil = 0;
        // Opt-in sampled depth (shadow maps). Depth is write-only unless set.
        bool shader_readable = false;
    };

    // Offscreen target description. The render module owns the target policy;
    // the RHI owns the texture attachments and their native representations.
    // Depth-only targets leave `color_attachments` empty; targets without depth
    // leave `depth` disengaged.
    struct RenderTargetDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t sample_count = 1;
        std::vector<RenderTargetColorAttachment> color_attachments;
        std::optional<RenderTargetDepthAttachment> depth;
    };

    // Whether two descriptions address the same physical storage, so a pooled
    // backend may hand the same target to both. Only the fields that decide the
    // images take part: a differing load or store operation still addresses the
    // same attachments.
    inline bool RenderTargetDescsShareStorage(const RenderTargetDesc &left,
                                              const RenderTargetDesc &right) noexcept
    {
        if (left.width != right.width || left.height != right.height ||
            left.sample_count != right.sample_count ||
            left.color_attachments.size() != right.color_attachments.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left.color_attachments.size(); ++index)
        {
            if (left.color_attachments[index].format != right.color_attachments[index].format)
            {
                return false;
            }
        }
        if (left.depth.has_value() != right.depth.has_value())
        {
            return false;
        }
        if (left.depth.has_value() &&
            (left.depth->format != right.depth->format ||
             left.depth->shader_readable != right.depth->shader_readable))
        {
            return false;
        }
        return true;
    }

    struct RenderTargetResource
    {
        std::vector<TextureHandle> color_attachments;
        TextureHandle depth; // invalid when the target has no depth
        RenderTargetDesc desc;
    };

    // Borrowed presentation data for a render target's first color attachment.
    // The values are backend-native tokens only; their lifetime stays with the
    // render target and callers must never destroy or retain them past resize.
    struct RenderTargetView
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uintptr_t native_image = 0;
        uintptr_t native_image_view = 0;

        bool IsValid() const
        {
            return width != 0 && height != 0 && native_image_view != 0;
        }
    };
}

#endif
