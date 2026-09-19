#include "vulkan_acceleration_structure_owner.h"

#include <algorithm>
#include <cstring>

#include "vulkan_buffer_manager.h"

namespace kpengine::graphics
{
    namespace
    {
        VkAccelerationStructureTypeKHR ToVulkanType(
            RayTracingAccelerationStructureType type)
        {
            return type == RayTracingAccelerationStructureType::TopLevel
                       ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                       : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        }

        VkDeviceSize GeometryPrimitiveCount(const RayTracingGeometryDesc &geometry)
        {
            return geometry.index_count / 3u;
        }
    }

    VulkanAccelerationStructureOwner::VulkanAccelerationStructureOwner(
        VkDevice device, VulkanBufferManager &buffer_manager)
        : device_(device), buffer_manager_(&buffer_manager), supported_(device != VK_NULL_HANDLE)
    {
        if (device_ != VK_NULL_HANDLE)
        {
            create_acceleration_structure_ =
                reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
                    vkGetDeviceProcAddr(device_, "vkCreateAccelerationStructureKHR"));
            destroy_acceleration_structure_ =
                reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
                    vkGetDeviceProcAddr(device_, "vkDestroyAccelerationStructureKHR"));
            get_acceleration_structure_address_ =
                reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
                    vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureDeviceAddressKHR"));
            get_build_sizes_ =
                reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
                    vkGetDeviceProcAddr(device_, "vkGetAccelerationStructureBuildSizesKHR"));
            cmd_build_acceleration_structures_ =
                reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
                    vkGetDeviceProcAddr(device_, "vkCmdBuildAccelerationStructuresKHR"));
            supported_ = create_acceleration_structure_ != nullptr &&
                         destroy_acceleration_structure_ != nullptr &&
                         get_acceleration_structure_address_ != nullptr &&
                         get_build_sizes_ != nullptr &&
                         cmd_build_acceleration_structures_ != nullptr;
        }
    }

    VulkanAccelerationStructureOwner::~VulkanAccelerationStructureOwner()
    {
        DestroyAll();
    }

    AccelerationStructureHandle VulkanAccelerationStructureOwner::CreateAccelerationStructure(
        const RayTracingAccelerationStructureDesc &desc)
    {
        if (!supported_ || !IsRayTracingAccelerationStructureDescValid(desc))
        {
            return {};
        }
        const AccelerationStructureHandle handle = handle_system_.Create();
        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        Resource &resource = resources_[handle.id];
        resource = {};
        resource.handle = handle;
        resource.desc = desc;
        resource.alive = true;
        return handle;
    }

    bool VulkanAccelerationStructureOwner::DestroyAccelerationStructure(
        AccelerationStructureHandle handle)
    {
        Resource *resource = GetResource(handle);
        if (!resource || resource->pending_destroy)
        {
            return false;
        }
        resource->pending_destroy = true;
        if (active_top_level_ == handle)
        {
            active_top_level_ = {};
        }
        return true;
    }

    RayTracingPipelineHandle VulkanAccelerationStructureOwner::CreateRayTracingPipeline(
        const RayTracingPipelineDesc &desc)
    {
        (void)desc;
        return {};
    }

    bool VulkanAccelerationStructureOwner::DestroyRayTracingPipeline(
        RayTracingPipelineHandle handle)
    {
        (void)handle;
        return false;
    }

    DescriptorSetHandle VulkanAccelerationStructureOwner::CreateRayTracingResourceBindingSet(
        RayTracingPipelineHandle pipeline, const RayTracingResourceBindingSetDesc &desc)
    {
        (void)pipeline;
        (void)desc;
        return {};
    }

    bool VulkanAccelerationStructureOwner::DestroyRayTracingResourceBindingSet(
        DescriptorSetHandle handle)
    {
        (void)handle;
        return false;
    }

    VulkanAccelerationStructureOwner::Resource *
    VulkanAccelerationStructureOwner::GetResource(AccelerationStructureHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        return index < resources_.size() && resources_[index].alive
                   ? &resources_[index]
                   : nullptr;
    }

    const VulkanAccelerationStructureOwner::Resource *
    VulkanAccelerationStructureOwner::GetResource(AccelerationStructureHandle handle) const
    {
        const uint32_t index = handle_system_.Get(handle);
        return index < resources_.size() && resources_[index].alive
                   ? &resources_[index]
                   : nullptr;
    }

    bool VulkanAccelerationStructureOwner::EnsureStorage(Resource &resource, VkDeviceSize size)
    {
        if (size == 0)
        {
            return false;
        }
        if (resource.acceleration_structure != VK_NULL_HANDLE && resource.storage_size >= size)
        {
            return true;
        }
        if (resource.acceleration_structure != VK_NULL_HANDLE || resource.storage_buffer.IsValid())
        {
            return false;
        }

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const BufferHandle storage = buffer_manager_->CreateBufferResource(
            device_, &buffer_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);

        const VulkanBufferResource *storage_resource = buffer_manager_->GetBufferResource(storage);
        if (!storage_resource)
        {
            buffer_manager_->DestroyBufferResource(device_, storage);
            return false;
        }

        VkAccelerationStructureCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create_info.buffer = storage_resource->buffer;
        create_info.size = size;
        create_info.type = ToVulkanType(resource.desc.type);
        VkAccelerationStructureKHR acceleration_structure = VK_NULL_HANDLE;
        if (create_acceleration_structure_(device_, &create_info, nullptr,
                                           &acceleration_structure) != VK_SUCCESS)
        {
            buffer_manager_->DestroyBufferResource(device_, storage);
            return false;
        }
        resource.storage_buffer = storage;
        resource.storage_size = size;
        resource.acceleration_structure = acceleration_structure;
        return true;
    }

    bool VulkanAccelerationStructureOwner::BuildOne(
        VkCommandBuffer command_buffer, const RayTracingBuildDesc &build,
        TemporaryBuffers &temporary_buffers)
    {
        Resource *resource = GetResource(build.target);
        if (!resource || resource->pending_destroy ||
            !IsRayTracingBuildDescValid(build) ||
            ((resource->desc.type == RayTracingAccelerationStructureType::BottomLevel) !=
             !build.geometries.empty()) ||
            (build.mode == RayTracingBuildMode::Update &&
             resource->acceleration_structure == VK_NULL_HANDLE))
        {
            return false;
        }

        std::vector<VkAccelerationStructureGeometryKHR> geometries;
        std::vector<uint32_t> primitive_counts;
        VkAccelerationStructureGeometryKHR instance_geometry{};

        if (!build.geometries.empty())
        {
            geometries.reserve(build.geometries.size());
            primitive_counts.reserve(build.geometries.size());
            for (const RayTracingGeometryDesc &geometry : build.geometries)
            {
                const VulkanBufferResource *vertex =
                    buffer_manager_->GetBufferResource(geometry.vertex_buffer);
                const VulkanBufferResource *index =
                    buffer_manager_->GetBufferResource(geometry.index_buffer);
                const VkDeviceAddress vertex_address =
                    buffer_manager_->GetDeviceAddress(device_, geometry.vertex_buffer);
                const VkDeviceAddress index_address =
                    buffer_manager_->GetDeviceAddress(device_, geometry.index_buffer);
                if (!vertex || !index || vertex_address == 0 || index_address == 0)
                {
                    return false;
                }

                VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
                triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                triangles.vertexData.deviceAddress = vertex_address + geometry.vertex_offset;
                triangles.vertexStride = geometry.vertex_stride;
                triangles.maxVertex = geometry.vertex_count - 1;
                triangles.indexType = geometry.index_type == RayTracingIndexType::UInt16
                                          ? VK_INDEX_TYPE_UINT16
                                          : VK_INDEX_TYPE_UINT32;
                triangles.indexData.deviceAddress = index_address + geometry.index_offset;

                VkAccelerationStructureGeometryKHR geometry_info{};
                geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                geometry_info.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geometry_info.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                geometry_info.geometry.triangles = triangles;
                geometries.push_back(geometry_info);
                primitive_counts.push_back(static_cast<uint32_t>(GeometryPrimitiveCount(geometry)));
            }
        }
        else
        {
            std::vector<VkAccelerationStructureInstanceKHR> instances;
            instances.reserve(build.instances.size());
            for (const RayTracingInstanceDesc &instance : build.instances)
            {
                const Resource *bottom_level = GetResource(instance.bottom_level);
                if (!bottom_level || bottom_level->acceleration_structure == VK_NULL_HANDLE)
                {
                    return false;
                }
                VkAccelerationStructureDeviceAddressInfoKHR address_info{};
                address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
                address_info.accelerationStructure = bottom_level->acceleration_structure;
                const VkDeviceAddress bottom_level_address =
                    get_acceleration_structure_address_(device_, &address_info);
                if (bottom_level_address == 0)
                {
                    return false;
                }

                VkAccelerationStructureInstanceKHR native_instance{};
                std::memcpy(native_instance.transform.matrix, instance.transform.data(),
                            sizeof(native_instance.transform.matrix));
                native_instance.instanceCustomIndex = instance.instance_id & 0x00FFFFFFu;
                native_instance.mask = instance.visibility_mask;
                native_instance.instanceShaderBindingTableRecordOffset = 0;
                native_instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                native_instance.accelerationStructureReference = bottom_level_address;
                instances.push_back(native_instance);
            }

            VkBufferCreateInfo instance_buffer_info{};
            instance_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            instance_buffer_info.size = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();
            instance_buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            instance_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            const BufferHandle instance_handle = buffer_manager_->CreateBufferResource(
                device_, &instance_buffer_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
            buffer_manager_->UploadData(instance_handle, instance_buffer_info.size, instances.data());
            temporary_buffers.handles.push_back(instance_handle);
            const VulkanBufferResource *instance_resource =
                buffer_manager_->GetBufferResource(instance_handle);
            const VkDeviceAddress instance_address =
                buffer_manager_->GetDeviceAddress(device_, instance_handle);
            if (!instance_resource || instance_address == 0)
            {
                return false;
            }
            VkAccelerationStructureGeometryInstancesDataKHR instances_data{};
            instances_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            instances_data.arrayOfPointers = VK_FALSE;
            instances_data.data.deviceAddress = instance_address;
            instance_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            instance_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            instance_geometry.geometry.instances = instances_data;
            geometries.push_back(instance_geometry);
            primitive_counts.push_back(static_cast<uint32_t>(build.instances.size()));
        }

        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type = ToVulkanType(resource->desc.type);
        build_info.flags = resource->desc.allow_update
                               ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
                               : 0;
        build_info.mode = build.mode == RayTracingBuildMode::Update
                              ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                              : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount = static_cast<uint32_t>(geometries.size());
        build_info.pGeometries = geometries.data();
        build_info.srcAccelerationStructure =
            build.mode == RayTracingBuildMode::Update ? resource->acceleration_structure
                                                        : VK_NULL_HANDLE;

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        get_build_sizes_(
            device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info,
            primitive_counts.data(), &sizes);
        if (!EnsureStorage(*resource, sizes.accelerationStructureSize))
        {
            return false;
        }

        const VkDeviceSize scratch_size = build.mode == RayTracingBuildMode::Update
                                              ? sizes.updateScratchSize
                                              : sizes.buildScratchSize;
        if (scratch_size == 0)
        {
            return false;
        }
        VkBufferCreateInfo scratch_info{};
        scratch_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        scratch_info.size = scratch_size;
        scratch_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        scratch_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const BufferHandle scratch_handle = buffer_manager_->CreateBufferResource(
            device_, &scratch_info, VulkanMemoryUsageType::MEMORY_USAGE_DEVICE);
        temporary_buffers.handles.push_back(scratch_handle);
        const VkDeviceAddress scratch_address =
            buffer_manager_->GetDeviceAddress(device_, scratch_handle);
        if (scratch_address == 0)
        {
            return false;
        }
        build_info.dstAccelerationStructure = resource->acceleration_structure;
        build_info.scratchData.deviceAddress = scratch_address;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(geometries.size());
        for (size_t index = 0; index < ranges.size(); ++index)
        {
            ranges[index].primitiveCount = primitive_counts[index];
        }
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> range_pointers{
            ranges.data()};
        cmd_build_acceleration_structures_(command_buffer, 1, &build_info,
                                           range_pointers.data());
        if (resource->desc.type == RayTracingAccelerationStructureType::TopLevel)
        {
            active_top_level_ = resource->handle;
        }
        return true;
    }

    bool VulkanAccelerationStructureOwner::Build(
        VkCommandBuffer command_buffer, std::span<const RayTracingBuildDesc> builds)
    {
        if (!supported_ || command_buffer == VK_NULL_HANDLE || builds.empty())
        {
            return false;
        }
        TemporaryBuffers temporary_buffers{};
        for (const RayTracingBuildDesc &build : builds)
        {
            if (!BuildOne(command_buffer, build, temporary_buffers))
            {
                if (!temporary_buffers.handles.empty())
                {
                    temporary_buffers_.push_back(std::move(temporary_buffers));
                }
                return false;
            }
        }
        if (!temporary_buffers.handles.empty())
        {
            temporary_buffers_.push_back(std::move(temporary_buffers));
        }
        return true;
    }

    bool VulkanAccelerationStructureOwner::RequireUsage(
        VkCommandBuffer command_buffer, AccelerationStructureHandle handle,
        ResourceUsage usage)
    {
        const Resource *resource = GetResource(handle);
        if (!supported_ || command_buffer == VK_NULL_HANDLE || !resource ||
            resource->acceleration_structure == VK_NULL_HANDLE)
        {
            return false;
        }
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = usage == ResourceUsage::AccelerationStructureRead
                                    ? VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
                                    : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command_buffer, &dependency);
        return true;
    }

    void VulkanAccelerationStructureOwner::DestroyResource(Resource &resource) noexcept
    {
        if (resource.acceleration_structure != VK_NULL_HANDLE)
        {
            destroy_acceleration_structure_(device_, resource.acceleration_structure, nullptr);
        }
        if (resource.storage_buffer.IsValid())
        {
            buffer_manager_->DestroyBufferResource(device_, resource.storage_buffer);
        }
        resource = {};
    }

    void VulkanAccelerationStructureOwner::DestroyTemporaryBuffers(
        TemporaryBuffers &temporary_buffers) noexcept
    {
        for (const BufferHandle handle : temporary_buffers.handles)
        {
            buffer_manager_->DestroyBufferResource(device_, handle);
        }
        temporary_buffers = {};
    }

    void VulkanAccelerationStructureOwner::CollectCompleted(uint64_t completed_submission_serial)
    {
        for (uint32_t index = 0; index < resources_.size(); ++index)
        {
            Resource &resource = resources_[index];
            if (resource.alive && resource.pending_destroy && resource.retire_serial != 0 &&
                resource.retire_serial <= completed_submission_serial)
            {
                const AccelerationStructureHandle handle = resource.handle;
                if (active_top_level_ == handle)
                {
                    active_top_level_ = {};
                }
                DestroyResource(resource);
                handle_system_.Destroy(handle);
            }
        }
        for (TemporaryBuffers &temporary_buffers : temporary_buffers_)
        {
            if (temporary_buffers.retire_serial != 0 &&
                temporary_buffers.retire_serial <= completed_submission_serial)
            {
                DestroyTemporaryBuffers(temporary_buffers);
            }
        }
        temporary_buffers_.erase(
            std::remove_if(temporary_buffers_.begin(), temporary_buffers_.end(),
                           [](const TemporaryBuffers &buffers) { return buffers.handles.empty(); }),
            temporary_buffers_.end());
    }

    void VulkanAccelerationStructureOwner::RetireSubmitted(uint64_t submission_serial)
    {
        for (Resource &resource : resources_)
        {
            if (resource.alive && resource.pending_destroy && resource.retire_serial == 0)
            {
                resource.retire_serial = submission_serial;
            }
        }
        for (TemporaryBuffers &temporary_buffers : temporary_buffers_)
        {
            if (temporary_buffers.retire_serial == 0)
            {
                temporary_buffers.retire_serial = submission_serial;
            }
        }
    }

    void VulkanAccelerationStructureOwner::DestroyAll() noexcept
    {
        for (TemporaryBuffers &temporary_buffers : temporary_buffers_)
        {
            DestroyTemporaryBuffers(temporary_buffers);
        }
        temporary_buffers_.clear();
        for (Resource &resource : resources_)
        {
            if (resource.alive)
            {
                if (active_top_level_ == resource.handle)
                {
                    active_top_level_ = {};
                }
                DestroyResource(resource);
            }
        }
    }
}
