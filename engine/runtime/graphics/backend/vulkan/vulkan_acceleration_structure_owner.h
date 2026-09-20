#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_ACCELERATION_STRUCTURE_OWNER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_ACCELERATION_STRUCTURE_OWNER_H

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "common/command_recorder.h"
#include "common/ray_tracing.h"
#include "base/base.h"

namespace kpengine::graphics
{
    class VulkanBufferManager;

    class VulkanAccelerationStructureOwner final : public RayTracingResourceOwner
    {
    public:
        VulkanAccelerationStructureOwner(VkDevice device, VulkanBufferManager &buffer_manager);
        ~VulkanAccelerationStructureOwner() override;

        VulkanAccelerationStructureOwner(const VulkanAccelerationStructureOwner &) = delete;
        VulkanAccelerationStructureOwner &operator=(const VulkanAccelerationStructureOwner &) = delete;

        bool IsSupported() const noexcept override { return supported_; }
        AccelerationStructureHandle CreateAccelerationStructure(
            const RayTracingAccelerationStructureDesc &desc) override;
        bool DestroyAccelerationStructure(AccelerationStructureHandle handle) override;
        RayTracingPipelineHandle CreateRayTracingPipeline(
            const RayTracingPipelineDesc &desc) override;
        bool DestroyRayTracingPipeline(RayTracingPipelineHandle handle) override;
        DescriptorSetHandle CreateRayTracingResourceBindingSet(
            RayTracingPipelineHandle pipeline,
            const RayTracingResourceBindingSetDesc &desc) override;
        bool DestroyRayTracingResourceBindingSet(DescriptorSetHandle handle) override;
        void CollectCompleted(uint64_t completed_submission_serial) override;
        void RetireSubmitted(uint64_t submission_serial) override;

        bool Build(VkCommandBuffer command_buffer,
                   std::span<const RayTracingBuildDesc> builds);
        AccelerationStructureHandle GetActiveTopLevel() const noexcept
        {
            return active_top_level_;
        }
        VkAccelerationStructureKHR GetNativeAccelerationStructure(
            AccelerationStructureHandle handle) const noexcept;
        bool RequireUsage(VkCommandBuffer command_buffer,
                          AccelerationStructureHandle handle,
                          ResourceUsage usage);

    private:
        struct Resource
        {
            AccelerationStructureHandle handle{};
            RayTracingAccelerationStructureDesc desc{};
            VkAccelerationStructureKHR acceleration_structure = VK_NULL_HANDLE;
            BufferHandle storage_buffer{};
            VkDeviceSize storage_size = 0;
            bool alive = false;
            bool pending_destroy = false;
            uint64_t retire_serial = 0;
        };

        struct TemporaryBuffers
        {
            std::vector<BufferHandle> handles;
            uint64_t retire_serial = 0;
        };

        Resource *GetResource(AccelerationStructureHandle handle);
        const Resource *GetResource(AccelerationStructureHandle handle) const;
        bool BuildOne(VkCommandBuffer command_buffer, const RayTracingBuildDesc &build,
                      TemporaryBuffers &temporary_buffers);
        bool EnsureStorage(Resource &resource, VkDeviceSize size);
        void DestroyResource(Resource &resource) noexcept;
        void DestroyTemporaryBuffers(TemporaryBuffers &temporary_buffers) noexcept;
        void DestroyAll() noexcept;

        VkDevice device_ = VK_NULL_HANDLE;
        VulkanBufferManager *buffer_manager_ = nullptr;
        HandleSystem<AccelerationStructureHandle> handle_system_;
        std::vector<Resource> resources_;
        std::vector<TemporaryBuffers> temporary_buffers_;
        AccelerationStructureHandle active_top_level_{};
        PFN_vkCreateAccelerationStructureKHR create_acceleration_structure_ = nullptr;
        PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure_ = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR get_acceleration_structure_address_ = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR get_build_sizes_ = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_acceleration_structures_ = nullptr;
        bool supported_ = false;
    };
}

#endif
