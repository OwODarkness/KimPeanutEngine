#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_FREE_RANGE_LIST_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_FREE_RANGE_LIST_H

#include <algorithm>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

namespace kpengine::graphics
{
    struct VulkanMemoryFreeRange
    {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    // Pure range bookkeeping kept separate from VkDeviceMemory ownership so it
    // can be exhaustively tested without a Vulkan device.
    class VulkanMemoryFreeRangeList
    {
    public:
        void Reset(VkDeviceSize size)
        {
            ranges_.clear();
            if (size != 0)
            {
                ranges_.push_back({0, size});
            }
        }

        bool CanAllocate(VkDeviceSize size, VkDeviceSize alignment) const
        {
            for (const VulkanMemoryFreeRange &range : ranges_)
            {
                const VkDeviceSize offset = AlignUp(range.offset, alignment);
                if (offset >= range.offset && size <= range.size - (offset - range.offset))
                {
                    return true;
                }
            }
            return false;
        }

        std::optional<VkDeviceSize> Allocate(VkDeviceSize size, VkDeviceSize alignment)
        {
            if (size == 0)
            {
                return std::nullopt;
            }
            for (size_t index = 0; index < ranges_.size(); ++index)
            {
                const VulkanMemoryFreeRange range = ranges_[index];
                const VkDeviceSize offset = AlignUp(range.offset, alignment);
                const VkDeviceSize prefix = offset - range.offset;
                if (prefix > range.size || size > range.size - prefix)
                {
                    continue;
                }
                const VkDeviceSize suffix = range.size - prefix - size;
                ranges_.erase(ranges_.begin() + index);
                if (prefix != 0)
                {
                    ranges_.push_back({range.offset, prefix});
                }
                if (suffix != 0)
                {
                    ranges_.push_back({offset + size, suffix});
                }
                return offset;
            }
            return std::nullopt;
        }

        void Free(VkDeviceSize offset, VkDeviceSize size)
        {
            if (size == 0)
            {
                return;
            }
            ranges_.push_back({offset, size});
            std::sort(ranges_.begin(), ranges_.end(), [](const auto &left, const auto &right) {
                return left.offset < right.offset;
            });
            std::vector<VulkanMemoryFreeRange> merged;
            for (const VulkanMemoryFreeRange &range : ranges_)
            {
                if (!merged.empty() && merged.back().offset + merged.back().size == range.offset)
                {
                    merged.back().size += range.size;
                }
                else
                {
                    merged.push_back(range);
                }
            }
            ranges_ = std::move(merged);
        }

        const std::vector<VulkanMemoryFreeRange> &Ranges() const noexcept { return ranges_; }

    private:
        static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
        }

        std::vector<VulkanMemoryFreeRange> ranges_;
    };
}

#endif
