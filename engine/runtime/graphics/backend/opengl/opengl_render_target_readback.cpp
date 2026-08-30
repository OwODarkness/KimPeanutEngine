#include "opengl_render_target_readback.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace kpengine::graphics
{
    namespace
    {
        constexpr uint32_t kBytesPerPixel = 4;

        size_t GetByteCount(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0 ||
                static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height)
            {
                return 0;
            }
            const size_t pixel_count = static_cast<size_t>(width) * height;
            return pixel_count <= std::numeric_limits<size_t>::max() / kBytesPerPixel
                       ? pixel_count * kBytesPerPixel
                       : 0;
        }
    }

    OpenglRenderTargetReadback::OpenglRenderTargetReadback(
        std::function<OpenglRenderTargetReadbackSource(RenderTargetHandle)> resolve_source)
        : resolve_source_(std::move(resolve_source))
    {
    }

    bool OpenglRenderTargetReadback::EnqueueRenderTargetReadback(
        RenderTargetReadbackRequest request, RenderTargetReadbackCallback on_completed)
    {
        if (!on_completed)
        {
            return false;
        }
        if (!request.IsValid() || !resolve_source_(request.target).IsValid())
        {
            on_completed({RenderTargetReadbackStatus::InvalidTarget, {},
                          "OpenGL render-target readback source is invalid"});
            return true;
        }
        std::scoped_lock lock(mutex_);
        queued_.push_back({request, std::move(on_completed)});
        return true;
    }

    void OpenglRenderTargetReadback::CollectCompletedReadbacks()
    {
        std::vector<PendingReadback> pending;
        {
            std::scoped_lock lock(mutex_);
            pending.swap(queued_);
        }

        for (PendingReadback &entry : pending)
        {
            const OpenglRenderTargetReadbackSource source = resolve_source_(entry.request.target);
            const size_t byte_count = GetByteCount(source.width, source.height);
            if (!source.IsValid() || byte_count == 0)
            {
                entry.on_completed({RenderTargetReadbackStatus::Failed, {},
                                    "OpenGL readback source was invalid at collection time"});
                continue;
            }

            CapturedImage image{};
            image.width = source.width;
            image.height = source.height;
            image.frame_number = entry.request.frame_number;
            image.rgba8_pixels.resize(byte_count);
            glGetError();
            glGetTextureSubImage(source.image, 0, 0, 0, 0,
                                 static_cast<GLsizei>(source.width),
                                 static_cast<GLsizei>(source.height), 1,
                                 GL_RGBA, GL_UNSIGNED_BYTE,
                                 static_cast<GLsizei>(byte_count), image.rgba8_pixels.data());
            if (glGetError() != GL_NO_ERROR)
            {
                entry.on_completed({RenderTargetReadbackStatus::Failed, {},
                                    "OpenGL texture readback failed"});
                continue;
            }
            // OpenGL returns texture rows from its lower-left origin. CapturedImage
            // uses the same top-left row order as Vulkan and the image codecs.
            const size_t row_byte_count = static_cast<size_t>(source.width) * kBytesPerPixel;
            for (size_t row = 0; row < source.height / 2; ++row)
            {
                auto top = image.rgba8_pixels.begin() + row * row_byte_count;
                auto bottom = image.rgba8_pixels.begin() +
                              (static_cast<size_t>(source.height) - 1 - row) * row_byte_count;
                std::swap_ranges(top, top + row_byte_count, bottom);
            }
            entry.on_completed({RenderTargetReadbackStatus::Captured, std::move(image), {}});
        }
    }

    void OpenglRenderTargetReadback::CancelTarget(RenderTargetHandle target, std::string diagnostic)
    {
        std::vector<PendingReadback> cancelled;
        {
            std::scoped_lock lock(mutex_);
            auto first_remaining = queued_.begin();
            for (auto current = queued_.begin(); current != queued_.end(); ++current)
            {
                if (current->request.target == target)
                {
                    cancelled.push_back(std::move(*current));
                }
                else
                {
                    *first_remaining++ = std::move(*current);
                }
            }
            queued_.erase(first_remaining, queued_.end());
        }
        CompleteCancelled(std::move(cancelled), diagnostic);
    }

    void OpenglRenderTargetReadback::DrainPendingReadbacks(std::string diagnostic)
    {
        std::vector<PendingReadback> cancelled;
        {
            std::scoped_lock lock(mutex_);
            cancelled.swap(queued_);
        }
        CompleteCancelled(std::move(cancelled), diagnostic);
    }

    void OpenglRenderTargetReadback::CompleteCancelled(std::vector<PendingReadback> pending,
                                                        const std::string &diagnostic)
    {
        for (PendingReadback &entry : pending)
        {
            entry.on_completed({RenderTargetReadbackStatus::Cancelled, {}, diagnostic});
        }
    }
}
