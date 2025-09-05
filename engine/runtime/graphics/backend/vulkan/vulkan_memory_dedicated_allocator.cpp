#include "vulkan_memory_dedicated_allocator.h"

#include "log/logger.h"
namespace kpengine::graphics
{

  MemoryAllocation DedicatedAllocator::Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
  {
    MemoryAllocation memory_allocation{};
    VkMemoryAllocateInfo memory_allocate_info{};

    memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_allocate_info.allocationSize = size;
    memory_allocate_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &memory_allocation.memory) != VK_SUCCESS)
    {
      KP_LOG("DedicatedAllocationLog", LOG_LEVEL_ERROR, "Failed to allocate the memory");
      throw std::runtime_error("Failed to allocate the memory");
    }
    allocated_memory_blocks_.push_back(memory_allocation.memory);

    memory_allocation.offset = 0;
    memory_allocation.size = size;
    memory_allocation.owner = this;
    return memory_allocation;
  }
  void DedicatedAllocator::Free(VkDevice logicial_device, VkDeviceMemory memory)
  {
    vkFreeMemory(logicial_device, memory, nullptr);
    allocated_memory_blocks_.erase(std::remove(allocated_memory_blocks_.begin(), allocated_memory_blocks_.end(), memory), allocated_memory_blocks_.end());
  }

  void DedicatedAllocator::Destroy(VkDevice logicial_device)
  {
    for (size_t i = 0; i < allocated_memory_blocks_.size(); i++)
    {
      vkFreeMemory(logicial_device, allocated_memory_blocks_[i], nullptr);
    }
    allocated_memory_blocks_.clear();
  }
}