#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_RAY_TRACING_VALIDATION_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_RAY_TRACING_VALIDATION_H

#include <cstdint>
#include <limits>

namespace kpengine::graphics::vulkan_detail
{
    struct ShaderBindingTableLayout
    {
        uint32_t stride = 0;
        uint64_t region_size = 0;
        uint64_t data_size = 0;
    };

    constexpr uint64_t GreatestCommonDivisor(uint64_t left, uint64_t right) noexcept
    {
        while (right != 0)
        {
            const uint64_t remainder = left % right;
            left = right;
            right = remainder;
        }
        return left;
    }

    constexpr bool TryComputeShaderBindingTableLayout(
        uint32_t handle_size, uint32_t handle_alignment, uint32_t base_alignment,
        uint32_t maximum_stride, uint32_t group_count,
        ShaderBindingTableLayout &out) noexcept
    {
        out = {};
        if (handle_size == 0 || handle_alignment == 0 || base_alignment == 0 ||
            maximum_stride == 0 || group_count == 0)
        {
            return false;
        }

        const uint64_t divisor = GreatestCommonDivisor(handle_alignment, base_alignment);
        const uint64_t reduced_base = base_alignment / divisor;
        if (reduced_base > std::numeric_limits<uint64_t>::max() / handle_alignment)
        {
            return false;
        }
        const uint64_t alignment = reduced_base * handle_alignment;
        if (static_cast<uint64_t>(handle_size) >
            std::numeric_limits<uint64_t>::max() - (alignment - 1))
        {
            return false;
        }
        const uint64_t stride =
            ((static_cast<uint64_t>(handle_size) + alignment - 1) / alignment) * alignment;
        if (stride > maximum_stride || stride > std::numeric_limits<uint32_t>::max() ||
            stride > std::numeric_limits<uint64_t>::max() / group_count)
        {
            return false;
        }

        out.stride = static_cast<uint32_t>(stride);
        out.region_size = stride;
        out.data_size = stride * group_count;
        return true;
    }

    constexpr bool TryAlignShaderBindingTableAddress(
        uint64_t allocation_address, uint32_t base_alignment, uint64_t data_size,
        uint64_t allocation_size, uint64_t &out_offset,
        uint64_t &out_address) noexcept
    {
        out_offset = 0;
        out_address = 0;
        if (allocation_address == 0 || base_alignment == 0 || data_size == 0 ||
            allocation_size < data_size)
        {
            return false;
        }

        const uint64_t remainder = allocation_address % base_alignment;
        const uint64_t offset = remainder == 0 ? 0 : base_alignment - remainder;
        if (offset > allocation_size - data_size ||
            allocation_address > std::numeric_limits<uint64_t>::max() - offset)
        {
            return false;
        }

        out_offset = offset;
        out_address = allocation_address + offset;
        return true;
    }

    constexpr bool IsRayTracingDispatchWithinLimit(
        uint32_t width, uint32_t height, uint32_t depth,
        uint32_t maximum_invocations) noexcept
    {
        if (width == 0 || height == 0 || depth == 0 || maximum_invocations == 0 ||
            width > maximum_invocations || height > maximum_invocations ||
            depth > maximum_invocations)
        {
            return false;
        }

        const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
        if (pixel_count > maximum_invocations)
        {
            return false;
        }
        return depth <= maximum_invocations / pixel_count;
    }

    constexpr bool IsRayTracingRecursionDepthWithinLimit(
        uint32_t requested_depth, uint32_t maximum_depth) noexcept
    {
        return requested_depth > 0 && maximum_depth > 0 &&
               requested_depth <= maximum_depth;
    }
}

#endif
