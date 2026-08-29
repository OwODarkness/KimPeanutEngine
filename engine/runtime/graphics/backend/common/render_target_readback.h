#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_READBACK_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_READBACK_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "api.h"

namespace kpengine::graphics
{
    // Backend-normalized readback output. Pixels are tightly packed RGBA8 and
    // owned by the caller once the completion callback runs.
    struct CapturedImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t frame_number = 0;
        uint64_t submission_serial = 0;
        std::vector<uint8_t> rgba8_pixels;

        size_t ExpectedByteCount() const
        {
            constexpr size_t k_bytes_per_pixel = 4;
            if (width == 0 || height == 0 ||
                static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height)
            {
                return 0;
            }
            const size_t pixel_count = static_cast<size_t>(width) * height;
            if (pixel_count > std::numeric_limits<size_t>::max() / k_bytes_per_pixel)
            {
                return 0;
            }
            return pixel_count * k_bytes_per_pixel;
        }

        bool IsValid() const
        {
            const size_t expected_byte_count = ExpectedByteCount();
            return expected_byte_count != 0 && rgba8_pixels.size() == expected_byte_count;
        }
    };

    struct RenderTargetReadbackRequest
    {
        RenderTargetHandle target;
        uint64_t frame_number = 0;

        bool IsValid() const { return target.IsValid(); }
    };

    enum class RenderTargetReadbackStatus : uint8_t
    {
        Captured,
        InvalidTarget,
        Cancelled,
        Failed,
    };

    struct RenderTargetReadbackResult
    {
        RenderTargetReadbackStatus status = RenderTargetReadbackStatus::Failed;
        CapturedImage image;
        std::string diagnostic;

        bool IsSuccess() const
        {
            return status == RenderTargetReadbackStatus::Captured && image.IsValid();
        }
    };

    using RenderTargetReadbackCallback = std::function<void(RenderTargetReadbackResult)>;

    // Legal transitions for a backend-private staging/readback record. C3
    // supplies the native fence/PBO state; this contract supplies only the
    // portable lifetime vocabulary.
    enum class RenderTargetReadbackState : uint8_t
    {
        Queued,
        Submitted,
        Completed,
        Failed,
        Cancelled,
    };

    constexpr bool IsRenderTargetReadbackTransitionValid(RenderTargetReadbackState from,
                                                          RenderTargetReadbackState to)
    {
        return (from == RenderTargetReadbackState::Queued &&
                (to == RenderTargetReadbackState::Submitted ||
                 to == RenderTargetReadbackState::Failed ||
                 to == RenderTargetReadbackState::Cancelled)) ||
               (from == RenderTargetReadbackState::Submitted &&
                (to == RenderTargetReadbackState::Completed ||
                 to == RenderTargetReadbackState::Failed ||
                 to == RenderTargetReadbackState::Cancelled));
    }

    // C2 contract only. C3 attaches this internal interface to each backend;
    // callers never see native texture/image or staging-buffer objects.
    class IRenderTargetReadback
    {
    public:
        virtual ~IRenderTargetReadback() = default;

        virtual bool EnqueueRenderTargetReadback(RenderTargetReadbackRequest request,
                                                 RenderTargetReadbackCallback on_completed) = 0;
        // Called after backend fence/query progress so completed callbacks can
        // receive CPU-owned pixels.
        virtual void CollectCompletedReadbacks() = 0;
        // Called before resize replacement or backend shutdown destroys a
        // referenced attachment; every pending request must become Cancelled.
        virtual void DrainPendingReadbacks(std::string diagnostic) = 0;
    };
}

#endif
