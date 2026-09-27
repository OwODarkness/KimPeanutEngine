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
    class TextureManager;
    class SamplerManager;

    class VulkanAccelerationStructureOwner final : public RayTracingResourceOwner
    {
    public:
        struct LifecycleCounts
        {
            uint32_t acceleration_structures = 0;
            uint32_t pending_acceleration_structures = 0;
            uint32_t pipelines = 0;
            uint32_t descriptor_sets = 0;
            uint32_t pending_descriptor_sets = 0;
            uint32_t address_table_buffers = 0;
            uint32_t temporary_buffer_batches = 0;
        };

        VulkanAccelerationStructureOwner(VkPhysicalDevice physical_device, VkDevice device,
                                         VulkanBufferManager &buffer_manager,
                                         TextureManager &texture_manager,
                                         SamplerManager &sampler_manager);
        ~VulkanAccelerationStructureOwner() override;

        VulkanAccelerationStructureOwner(const VulkanAccelerationStructureOwner &) = delete;
        VulkanAccelerationStructureOwner &operator=(const VulkanAccelerationStructureOwner &) = delete;

        bool IsSupported() const noexcept override { return supported_; }
        void SetBindlessTextureLayout(VkDescriptorSetLayout layout) noexcept
        {
            bindless_texture_layout_ = layout;
        }
        LifecycleCounts GetLifecycleCounts() const noexcept;
        RayTracingResourceProfileCounters GetProfileCounters() const noexcept override
        {
            return profile_counters_;
        }
        void ResetProfileCounters() noexcept override { profile_counters_ = {}; }
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
        VkPipeline GetNativeRayTracingPipeline(RayTracingPipelineHandle handle) const noexcept;
        VkPipelineLayout GetRayTracingPipelineLayout(
            RayTracingPipelineHandle handle) const noexcept;
        bool UsesBindlessTextureTable(RayTracingPipelineHandle handle) const noexcept;
        VkDescriptorSet GetRayTracingDescriptorSet(DescriptorSetHandle handle) const noexcept;
        uint32_t GetRayTracingDescriptorSetIndex(DescriptorSetHandle handle) const noexcept;
        bool GetRayTracingShaderBindingTable(
            RayTracingPipelineHandle handle, VkStridedDeviceAddressRegionKHR &raygen,
            VkStridedDeviceAddressRegionKHR &miss, VkStridedDeviceAddressRegionKHR &hit) const;
        bool TraceRays(VkCommandBuffer command_buffer, const RayTracingDispatchDesc &dispatch) const;
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

        struct RayTracingPipelineResource
        {
            RayTracingPipelineHandle handle{};
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            struct DescriptorSetLayout
            {
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                std::vector<VkDescriptorSetLayoutBinding> bindings;
            };
            std::vector<DescriptorSetLayout> descriptor_set_layouts;
            BufferHandle shader_binding_table{};
            VkStridedDeviceAddressRegionKHR raygen_region{};
            VkStridedDeviceAddressRegionKHR miss_region{};
            VkStridedDeviceAddressRegionKHR hit_region{};
            bool alive = false;
        };

        struct RayTracingDescriptorSetResource
        {
            DescriptorSetHandle handle{};
            VkDescriptorPool pool = VK_NULL_HANDLE;
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
            RayTracingPipelineHandle pipeline{};
            uint32_t set = 0;
            std::vector<BufferHandle> owned_address_table_buffers;
            bool alive = false;
            bool pending_destroy = false;
            uint64_t retire_serial = 0;
        };

        Resource *GetResource(AccelerationStructureHandle handle);
        const Resource *GetResource(AccelerationStructureHandle handle) const;
        bool BuildOne(VkCommandBuffer command_buffer, const RayTracingBuildDesc &build,
                      TemporaryBuffers &temporary_buffers);
        bool EnsureStorage(Resource &resource, VkDeviceSize size);
        void DestroyResource(Resource &resource) noexcept;
        void DestroyTemporaryBuffers(TemporaryBuffers &temporary_buffers) noexcept;
        void DestroyRayTracingPipelineResource(RayTracingPipelineResource &resource) noexcept;
        void DestroyRayTracingDescriptorSet(RayTracingDescriptorSetResource &resource) noexcept;
        void DestroyAll() noexcept;

        VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VkDescriptorSetLayout bindless_texture_layout_ = VK_NULL_HANDLE;
        VulkanBufferManager *buffer_manager_ = nullptr;
        TextureManager *texture_manager_ = nullptr;
        SamplerManager *sampler_manager_ = nullptr;
        HandleSystem<AccelerationStructureHandle> handle_system_;
        HandleSystem<RayTracingPipelineHandle> ray_tracing_pipeline_handle_system_;
        HandleSystem<DescriptorSetHandle> ray_tracing_descriptor_set_handle_system_;
        std::vector<Resource> resources_;
        std::vector<RayTracingPipelineResource> ray_tracing_pipelines_;
        std::vector<RayTracingDescriptorSetResource> ray_tracing_descriptor_sets_;
        std::vector<TemporaryBuffers> temporary_buffers_;
        AccelerationStructureHandle active_top_level_{};
        PFN_vkCreateAccelerationStructureKHR create_acceleration_structure_ = nullptr;
        PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure_ = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR get_acceleration_structure_address_ = nullptr;
        PFN_vkGetAccelerationStructureBuildSizesKHR get_build_sizes_ = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_acceleration_structures_ = nullptr;
        PFN_vkCreateRayTracingPipelinesKHR create_ray_tracing_pipelines_ = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR get_ray_tracing_shader_group_handles_ = nullptr;
        PFN_vkCmdTraceRaysKHR cmd_trace_rays_ = nullptr;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties_{};
        bool supported_ = false;
        bool ray_tracing_pipeline_supported_ = false;
        RayTracingResourceProfileCounters profile_counters_{};
    };
}

#endif
