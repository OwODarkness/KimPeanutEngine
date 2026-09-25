#include "vulkan_acceleration_structure_owner.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "asset/shader.h"
#include "common/sampler_manager.h"
#include "common/texture_manager.h"
#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_enum.h"
#include "vulkan_pipeline_manager.h"
#include "vulkan_ray_tracing_validation.h"
#include "vulkan_sampler.h"
#include "vulkan_texture.h"

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
        VkPhysicalDevice physical_device, VkDevice device, VulkanBufferManager &buffer_manager,
        TextureManager &texture_manager, SamplerManager &sampler_manager)
        : physical_device_(physical_device), device_(device), buffer_manager_(&buffer_manager),
          texture_manager_(&texture_manager), sampler_manager_(&sampler_manager),
          supported_(device != VK_NULL_HANDLE)
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
            create_ray_tracing_pipelines_ =
                reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
                    vkGetDeviceProcAddr(device_, "vkCreateRayTracingPipelinesKHR"));
            get_ray_tracing_shader_group_handles_ =
                reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
                    vkGetDeviceProcAddr(device_, "vkGetRayTracingShaderGroupHandlesKHR"));
            cmd_trace_rays_ = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
                vkGetDeviceProcAddr(device_, "vkCmdTraceRaysKHR"));
            supported_ = create_acceleration_structure_ != nullptr &&
                         destroy_acceleration_structure_ != nullptr &&
                         get_acceleration_structure_address_ != nullptr &&
                         get_build_sizes_ != nullptr &&
                         cmd_build_acceleration_structures_ != nullptr;
            ray_tracing_properties_.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &ray_tracing_properties_;
            vkGetPhysicalDeviceProperties2(physical_device_, &properties);
            ray_tracing_pipeline_supported_ =
                supported_ && create_ray_tracing_pipelines_ != nullptr &&
                get_ray_tracing_shader_group_handles_ != nullptr &&
                cmd_trace_rays_ != nullptr && ray_tracing_properties_.shaderGroupHandleSize > 0 &&
                ray_tracing_properties_.shaderGroupHandleAlignment > 0 &&
                ray_tracing_properties_.shaderGroupBaseAlignment > 0 &&
                ray_tracing_properties_.maxShaderGroupStride > 0 &&
                ray_tracing_properties_.maxRayRecursionDepth > 0 &&
                ray_tracing_properties_.maxRayDispatchInvocationCount > 0;
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
        if (desc.type == RayTracingAccelerationStructureType::TopLevel)
        {
            // The opaque handle is valid as soon as Graphics reserves the
            // owner slot; storage is created by the first graph build.
            active_top_level_ = handle;
        }
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
        if (!ray_tracing_pipeline_supported_ || !IsRayTracingPipelineDescValid(desc))
        {
            return {};
        }
        vulkan_detail::ShaderBindingTableLayout sbt_layout{};
        if (!vulkan_detail::IsRayTracingRecursionDepthWithinLimit(
                desc.max_recursion_depth, ray_tracing_properties_.maxRayRecursionDepth) ||
            !vulkan_detail::TryComputeShaderBindingTableLayout(
                ray_tracing_properties_.shaderGroupHandleSize,
                ray_tracing_properties_.shaderGroupHandleAlignment,
                ray_tracing_properties_.shaderGroupBaseAlignment,
                ray_tracing_properties_.maxShaderGroupStride, 3, sbt_layout))
        {
            return {};
        }

        auto create_shader_module = [this](const data::ShaderData &shader)
        {
            if (shader.byte_code.empty() || shader.byte_code.size() % sizeof(uint32_t) != 0)
            {
                return VkShaderModule{VK_NULL_HANDLE};
            }
            VkShaderModuleCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize = shader.byte_code.size();
            create_info.pCode = reinterpret_cast<const uint32_t *>(shader.byte_code.data());
            VkShaderModule module = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device_, &create_info, nullptr, &module) != VK_SUCCESS)
            {
                return VkShaderModule{VK_NULL_HANDLE};
            }
            return module;
        };

        const VkShaderModule raygen_module = create_shader_module(*desc.ray_generation_shader);
        const VkShaderModule miss_module = create_shader_module(*desc.miss_shader);
        const VkShaderModule closest_hit_module = create_shader_module(*desc.closest_hit_shader);
        if (raygen_module == VK_NULL_HANDLE || miss_module == VK_NULL_HANDLE ||
            closest_hit_module == VK_NULL_HANDLE)
        {
            if (raygen_module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, raygen_module, nullptr);
            if (miss_module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, miss_module, nullptr);
            if (closest_hit_module != VK_NULL_HANDLE)
                vkDestroyShaderModule(device_, closest_hit_module, nullptr);
            return {};
        }

        std::vector<RayTracingPipelineResource::DescriptorSetLayout> layouts;
        layouts.reserve(desc.descriptor_binding_descs.size());
        for (const auto &set_bindings : desc.descriptor_binding_descs)
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bindings.reserve(set_bindings.size());
            for (const DescriptorBindingDesc &binding : set_bindings)
            {
                VkDescriptorSetLayoutBinding native{};
                native.binding = binding.binding;
                native.descriptorCount = binding.descriptor_count;
                native.descriptorType = ConvertToVulkanDescriptorType(binding.descriptor_type);
                native.stageFlags = ConvertToVulkanShaderStageFlags(binding.stage_flag);
                if (binding.stage_flag == ShaderStage::SHADER_STAGE_RAYGEN ||
                    binding.stage_flag == ShaderStage::SHADER_STAGE_MISS ||
                    binding.stage_flag == ShaderStage::SHADER_STAGE_CLOSEST_HIT)
                {
                    native.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                         VK_SHADER_STAGE_MISS_BIT_KHR |
                                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                }
                if (native.descriptorCount == 0 || native.descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM ||
                    native.stageFlags == 0)
                {
                    for (const auto &layout : layouts)
                        vkDestroyDescriptorSetLayout(device_, layout.layout, nullptr);
                    vkDestroyShaderModule(device_, raygen_module, nullptr);
                    vkDestroyShaderModule(device_, miss_module, nullptr);
                    vkDestroyShaderModule(device_, closest_hit_module, nullptr);
                    return {};
                }
                bindings.push_back(native);
            }
            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
            layout_info.pBindings = bindings.data();
            RayTracingPipelineResource::DescriptorSetLayout layout{};
            layout.bindings = std::move(bindings);
            if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &layout.layout) !=
                VK_SUCCESS)
            {
                for (const auto &existing : layouts)
                    vkDestroyDescriptorSetLayout(device_, existing.layout, nullptr);
                vkDestroyShaderModule(device_, raygen_module, nullptr);
                vkDestroyShaderModule(device_, miss_module, nullptr);
                vkDestroyShaderModule(device_, closest_hit_module, nullptr);
                return {};
            }
            layouts.push_back(std::move(layout));
        }

        std::vector<VkDescriptorSetLayout> native_layouts;
        native_layouts.reserve(layouts.size());
        for (const auto &layout : layouts) native_layouts.push_back(layout.layout);
        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(native_layouts.size());
        pipeline_layout_info.pSetLayouts = native_layouts.data();
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout) !=
            VK_SUCCESS)
        {
            for (const auto &layout : layouts)
                vkDestroyDescriptorSetLayout(device_, layout.layout, nullptr);
            vkDestroyShaderModule(device_, raygen_module, nullptr);
            vkDestroyShaderModule(device_, miss_module, nullptr);
            vkDestroyShaderModule(device_, closest_hit_module, nullptr);
            return {};
        }

        const VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
             0, VK_SHADER_STAGE_RAYGEN_BIT_KHR, raygen_module, desc.ray_generation_shader->entry.c_str(), nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
             0, VK_SHADER_STAGE_MISS_BIT_KHR, miss_module, desc.miss_shader->entry.c_str(), nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
             0, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, closest_hit_module, desc.closest_hit_shader->entry.c_str(), nullptr}};
        VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
        groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
        groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
        groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
        groups[1] = groups[0];
        groups[1].generalShader = 1;
        groups[2] = groups[0];
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].generalShader = VK_SHADER_UNUSED_KHR;
        groups[2].closestHitShader = 2;

        VkRayTracingPipelineCreateInfoKHR pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipeline_info.stageCount = 3;
        pipeline_info.pStages = stages;
        pipeline_info.groupCount = 3;
        pipeline_info.pGroups = groups;
        pipeline_info.maxPipelineRayRecursionDepth = desc.max_recursion_depth;
        pipeline_info.layout = pipeline_layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (create_ray_tracing_pipelines_(device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                          &pipeline_info, nullptr, &pipeline) != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(device_, pipeline_layout, nullptr);
            for (const auto &layout : layouts)
                vkDestroyDescriptorSetLayout(device_, layout.layout, nullptr);
            vkDestroyShaderModule(device_, raygen_module, nullptr);
            vkDestroyShaderModule(device_, miss_module, nullptr);
            vkDestroyShaderModule(device_, closest_hit_module, nullptr);
            return {};
        }
        vkDestroyShaderModule(device_, raygen_module, nullptr);
        vkDestroyShaderModule(device_, miss_module, nullptr);
        vkDestroyShaderModule(device_, closest_hit_module, nullptr);

        const VkDeviceSize handle_size = ray_tracing_properties_.shaderGroupHandleSize;
        const VkDeviceSize handle_stride = sbt_layout.stride;
        const VkDeviceSize sbt_data_size = sbt_layout.data_size;
        const VkDeviceSize sbt_allocation_size = sbt_data_size +
            ray_tracing_properties_.shaderGroupBaseAlignment - 1;
        std::vector<uint8_t> handles(static_cast<size_t>(handle_size) * 3);
        if (get_ray_tracing_shader_group_handles_(device_, pipeline, 0, 3, handles.size(),
                                                  handles.data()) != VK_SUCCESS)
        {
            vkDestroyPipeline(device_, pipeline, nullptr);
            vkDestroyPipelineLayout(device_, pipeline_layout, nullptr);
            for (const auto &layout : layouts)
                vkDestroyDescriptorSetLayout(device_, layout.layout, nullptr);
            return {};
        }
        VkBufferCreateInfo sbt_info{};
        sbt_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbt_info.size = sbt_allocation_size;
        sbt_info.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        sbt_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        BufferHandle sbt = buffer_manager_->CreateBufferResource(
            device_, &sbt_info, VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM);
        const VkDeviceAddress allocation_address =
            buffer_manager_->GetDeviceAddress(device_, sbt);
        uint64_t sbt_offset = 0;
        uint64_t sbt_address = 0;
        if (!sbt.IsValid() || !vulkan_detail::TryAlignShaderBindingTableAddress(
                                  allocation_address,
                                  ray_tracing_properties_.shaderGroupBaseAlignment,
                                  sbt_data_size, sbt_allocation_size, sbt_offset, sbt_address))
        {
            if (sbt.IsValid()) buffer_manager_->DestroyBufferResource(device_, sbt);
            vkDestroyPipeline(device_, pipeline, nullptr);
            vkDestroyPipelineLayout(device_, pipeline_layout, nullptr);
            for (const auto &layout : layouts)
                vkDestroyDescriptorSetLayout(device_, layout.layout, nullptr);
            return {};
        }

        std::vector<uint8_t> sbt_data(static_cast<size_t>(sbt_allocation_size));
        for (uint32_t group = 0; group < 3; ++group)
        {
            std::memcpy(sbt_data.data() + sbt_offset + group * handle_stride,
                        handles.data() + group * handle_size, static_cast<size_t>(handle_size));
        }
        buffer_manager_->UploadData(sbt, sbt_allocation_size, sbt_data.data());

        const RayTracingPipelineHandle handle = ray_tracing_pipeline_handle_system_.Create();
        if (handle.id == ray_tracing_pipelines_.size()) ray_tracing_pipelines_.emplace_back();
        RayTracingPipelineResource &resource = ray_tracing_pipelines_[handle.id];
        resource = {};
        resource.handle = handle;
        resource.pipeline = pipeline;
        resource.layout = pipeline_layout;
        resource.descriptor_set_layouts = std::move(layouts);
        resource.shader_binding_table = sbt;
        resource.raygen_region = {sbt_address, handle_stride, handle_stride};
        resource.miss_region = {sbt_address + handle_stride, handle_stride, handle_stride};
        resource.hit_region = {sbt_address + handle_stride * 2, handle_stride, handle_stride};
        resource.alive = true;
        KP_LOG("VulkanRayTracing", LOG_LEVEL_INFO,
               "SBT ready: handle=%u handleAlign=%u baseAlign=%u maxStride=%u "
               "recursion=%u/%u maxInvocations=%u raygen=(%llu,%llu,%llu) "
               "miss=(%llu,%llu,%llu) hit=(%llu,%llu,%llu) callable=(0,0,0)",
               ray_tracing_properties_.shaderGroupHandleSize,
               ray_tracing_properties_.shaderGroupHandleAlignment,
               ray_tracing_properties_.shaderGroupBaseAlignment,
               ray_tracing_properties_.maxShaderGroupStride, desc.max_recursion_depth,
               ray_tracing_properties_.maxRayRecursionDepth,
               ray_tracing_properties_.maxRayDispatchInvocationCount,
               static_cast<unsigned long long>(resource.raygen_region.deviceAddress),
               static_cast<unsigned long long>(resource.raygen_region.stride),
               static_cast<unsigned long long>(resource.raygen_region.size),
               static_cast<unsigned long long>(resource.miss_region.deviceAddress),
               static_cast<unsigned long long>(resource.miss_region.stride),
               static_cast<unsigned long long>(resource.miss_region.size),
               static_cast<unsigned long long>(resource.hit_region.deviceAddress),
               static_cast<unsigned long long>(resource.hit_region.stride),
               static_cast<unsigned long long>(resource.hit_region.size));
        return handle;
    }

    bool VulkanAccelerationStructureOwner::DestroyRayTracingPipeline(
        RayTracingPipelineHandle handle)
    {
        const uint32_t index = ray_tracing_pipeline_handle_system_.Get(handle);
        if (index >= ray_tracing_pipelines_.size() || !ray_tracing_pipelines_[index].alive)
            return false;
        DestroyRayTracingPipelineResource(ray_tracing_pipelines_[index]);
        return ray_tracing_pipeline_handle_system_.Destroy(handle);
    }

    DescriptorSetHandle VulkanAccelerationStructureOwner::CreateRayTracingResourceBindingSet(
        RayTracingPipelineHandle pipeline, const RayTracingResourceBindingSetDesc &desc)
    {
        const uint32_t pipeline_index = ray_tracing_pipeline_handle_system_.Get(pipeline);
        if (pipeline_index >= ray_tracing_pipelines_.size() ||
            !ray_tracing_pipelines_[pipeline_index].alive ||
            desc.set >= ray_tracing_pipelines_[pipeline_index].descriptor_set_layouts.size())
            return {};
        const auto &pipeline_resource = ray_tracing_pipelines_[pipeline_index];
        const auto &layout = pipeline_resource.descriptor_set_layouts[desc.set];
        const auto get_type = [&layout](uint32_t binding) -> VkDescriptorType
        {
            const auto it = std::find_if(layout.bindings.begin(), layout.bindings.end(),
                                         [binding](const VkDescriptorSetLayoutBinding &candidate)
                                         { return candidate.binding == binding; });
            return it == layout.bindings.end() ? VK_DESCRIPTOR_TYPE_MAX_ENUM : it->descriptorType;
        };
        std::vector<VkDescriptorPoolSize> pool_sizes;
        auto add_pool_size = [&pool_sizes](VkDescriptorType type)
        {
            const auto it = std::find_if(pool_sizes.begin(), pool_sizes.end(),
                                         [type](const VkDescriptorPoolSize &size)
                                         { return size.type == type; });
            if (it == pool_sizes.end()) pool_sizes.push_back({type, 1});
            else ++const_cast<VkDescriptorPoolSize &>(*it).descriptorCount;
        };
        for (const auto &binding : desc.bindings)
        {
            const VkDescriptorType type = std::visit(
                [&get_type](const auto &value) { return get_type(value.binding); }, binding);
            if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) return {};
            add_pool_size(type);
        }
        if (pool_sizes.empty()) return {};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool) != VK_SUCCESS) return {};
        VkDescriptorSetLayout native_layout = layout.layout;
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = pool;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &native_layout;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocate_info, &descriptor_set) != VK_SUCCESS)
        {
            vkDestroyDescriptorPool(device_, pool, nullptr);
            return {};
        }

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> buffers;
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> acceleration_infos;
        std::vector<VkAccelerationStructureKHR> acceleration_structures;
        writes.reserve(desc.bindings.size());
        buffers.reserve(desc.bindings.size());
        images.reserve(desc.bindings.size());
        acceleration_infos.reserve(desc.bindings.size());
        acceleration_structures.reserve(desc.bindings.size());
        try
        {
            for (const auto &binding : desc.bindings)
            {
                std::visit([&](const auto &value)
                {
                    using Binding = std::decay_t<decltype(value)>;
                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = descriptor_set;
                    write.dstBinding = value.binding;
                    write.descriptorCount = 1;
                    const VkDescriptorType type = get_type(value.binding);
                    if constexpr (std::is_same_v<Binding, UniformBufferBinding> ||
                                  std::is_same_v<Binding, RayTracingStorageBufferBinding>)
                    {
                        VulkanBufferResource *buffer = buffer_manager_->GetBufferResource(value.buffer);
                        const VkDescriptorType expected =
                            std::is_same_v<Binding, UniformBufferBinding>
                                ? (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                       ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                       : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        if (!buffer || type != expected) throw std::runtime_error("invalid RT buffer binding");
                        buffers.push_back({buffer->buffer, value.offset,
                                           value.range == 0 ? VK_WHOLE_SIZE : value.range});
                        write.descriptorType = type;
                        write.pBufferInfo = &buffers.back();
                    }
                    else if constexpr (std::is_same_v<Binding, SampledTextureBinding>)
                    {
                        Texture *texture = texture_manager_->GetTexture(value.texture);
                        Sampler *sampler = sampler_manager_->GetSampler(value.sampler);
                        if (!texture || !sampler || type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                            throw std::runtime_error("invalid RT sampled texture binding");
                        const auto texture_resource = ConvertToVulkanTextureResource(texture->GetTextueHandle());
                        const auto sampler_resource = ConvertToVulkanSamplerResource(sampler->GetSampleHandle());
                        images.push_back({sampler_resource.sampler, texture_resource.view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                        write.descriptorType = type;
                        write.pImageInfo = &images.back();
                    }
                    else if constexpr (std::is_same_v<Binding, RayTracingStorageTextureBinding>)
                    {
                        Texture *texture = texture_manager_->GetTexture(value.texture);
                        if (!texture || (static_cast<uint32_t>(texture->settings_.usage) &
                                         static_cast<uint32_t>(TextureUsage::TEXTURE_USAGE_STORAGE)) == 0 ||
                            type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                            throw std::runtime_error("invalid RT storage image binding");
                        const auto texture_resource = ConvertToVulkanTextureResource(texture->GetTextueHandle());
                        images.push_back({VK_NULL_HANDLE, texture_resource.view, VK_IMAGE_LAYOUT_GENERAL});
                        write.descriptorType = type;
                        write.pImageInfo = &images.back();
                    }
                    else
                    {
                        acceleration_structures.push_back(
                            GetNativeAccelerationStructure(value.acceleration_structure));
                        if (acceleration_structures.back() == VK_NULL_HANDLE ||
                            type != VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                            throw std::runtime_error("invalid RT acceleration-structure binding");
                        acceleration_infos.push_back({});
                        auto &info = acceleration_infos.back();
                        info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                        info.accelerationStructureCount = 1;
                        info.pAccelerationStructures = &acceleration_structures.back();
                        write.descriptorType = type;
                        write.pNext = &info;
                    }
                    writes.push_back(write);
                }, binding);
            }
        }
        catch (...)
        {
            vkDestroyDescriptorPool(device_, pool, nullptr);
            return {};
        }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        const DescriptorSetHandle handle = ray_tracing_descriptor_set_handle_system_.Create();
        if (handle.id == ray_tracing_descriptor_sets_.size()) ray_tracing_descriptor_sets_.emplace_back();
        ray_tracing_descriptor_sets_[handle.id] = {handle, pool, descriptor_set, pipeline, desc.set, true};
        return handle;
    }

    bool VulkanAccelerationStructureOwner::DestroyRayTracingResourceBindingSet(
        DescriptorSetHandle handle)
    {
        const uint32_t index = ray_tracing_descriptor_set_handle_system_.Get(handle);
        if (index >= ray_tracing_descriptor_sets_.size() ||
            !ray_tracing_descriptor_sets_[index].alive ||
            ray_tracing_descriptor_sets_[index].pending_destroy)
            return false;
        ray_tracing_descriptor_sets_[index].pending_destroy = true;
        return true;
    }

    VkPipeline VulkanAccelerationStructureOwner::GetNativeRayTracingPipeline(
        RayTracingPipelineHandle handle) const noexcept
    {
        const uint32_t index = ray_tracing_pipeline_handle_system_.Get(handle);
        return index < ray_tracing_pipelines_.size() && ray_tracing_pipelines_[index].alive
                   ? ray_tracing_pipelines_[index].pipeline : VK_NULL_HANDLE;
    }

    VkPipelineLayout VulkanAccelerationStructureOwner::GetRayTracingPipelineLayout(
        RayTracingPipelineHandle handle) const noexcept
    {
        const uint32_t index = ray_tracing_pipeline_handle_system_.Get(handle);
        return index < ray_tracing_pipelines_.size() && ray_tracing_pipelines_[index].alive
                   ? ray_tracing_pipelines_[index].layout : VK_NULL_HANDLE;
    }

    VkDescriptorSet VulkanAccelerationStructureOwner::GetRayTracingDescriptorSet(
        DescriptorSetHandle handle) const noexcept
    {
        const uint32_t index = ray_tracing_descriptor_set_handle_system_.Get(handle);
        return index < ray_tracing_descriptor_sets_.size() && ray_tracing_descriptor_sets_[index].alive
                   ? ray_tracing_descriptor_sets_[index].descriptor_set : VK_NULL_HANDLE;
    }

    uint32_t VulkanAccelerationStructureOwner::GetRayTracingDescriptorSetIndex(
        DescriptorSetHandle handle) const noexcept
    {
        const uint32_t index = ray_tracing_descriptor_set_handle_system_.Get(handle);
        return index < ray_tracing_descriptor_sets_.size() && ray_tracing_descriptor_sets_[index].alive
                   ? ray_tracing_descriptor_sets_[index].set : UINT32_MAX;
    }

    bool VulkanAccelerationStructureOwner::GetRayTracingShaderBindingTable(
        RayTracingPipelineHandle handle, VkStridedDeviceAddressRegionKHR &raygen,
        VkStridedDeviceAddressRegionKHR &miss, VkStridedDeviceAddressRegionKHR &hit) const
    {
        const uint32_t index = ray_tracing_pipeline_handle_system_.Get(handle);
        if (index >= ray_tracing_pipelines_.size() || !ray_tracing_pipelines_[index].alive) return false;
        raygen = ray_tracing_pipelines_[index].raygen_region;
        miss = ray_tracing_pipelines_[index].miss_region;
        hit = ray_tracing_pipelines_[index].hit_region;
        return true;
    }

    bool VulkanAccelerationStructureOwner::TraceRays(
        VkCommandBuffer command_buffer, const RayTracingDispatchDesc &dispatch) const
    {
        if (!ray_tracing_pipeline_supported_ || command_buffer == VK_NULL_HANDLE ||
            !IsRayTracingDispatchDescValid(dispatch) || cmd_trace_rays_ == nullptr ||
            !vulkan_detail::IsRayTracingDispatchWithinLimit(
                dispatch.width, dispatch.height, dispatch.depth,
                ray_tracing_properties_.maxRayDispatchInvocationCount))
            return false;
        const uint32_t pipeline_index = ray_tracing_pipeline_handle_system_.Get(dispatch.pipeline);
        const uint32_t descriptor_index =
            ray_tracing_descriptor_set_handle_system_.Get(dispatch.bindings);
        if (pipeline_index >= ray_tracing_pipelines_.size() ||
            descriptor_index >= ray_tracing_descriptor_sets_.size() ||
            !ray_tracing_pipelines_[pipeline_index].alive ||
            !ray_tracing_descriptor_sets_[descriptor_index].alive ||
            ray_tracing_descriptor_sets_[descriptor_index].pipeline != dispatch.pipeline)
            return false;
        VkStridedDeviceAddressRegionKHR raygen{};
        VkStridedDeviceAddressRegionKHR miss{};
        VkStridedDeviceAddressRegionKHR hit{};
        const VkStridedDeviceAddressRegionKHR callable{};
        if (!GetRayTracingShaderBindingTable(dispatch.pipeline, raygen, miss, hit)) return false;
        cmd_trace_rays_(command_buffer, &raygen, &miss, &hit, &callable,
                        dispatch.width, dispatch.height, dispatch.depth);
        return true;
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

    VkAccelerationStructureKHR VulkanAccelerationStructureOwner::GetNativeAccelerationStructure(
        AccelerationStructureHandle handle) const noexcept
    {
        const Resource *const resource = GetResource(handle);
        return resource != nullptr ? resource->acceleration_structure : VK_NULL_HANDLE;
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
        if (!supported_ || command_buffer == VK_NULL_HANDLE || !resource)
        {
            return false;
        }
        if (usage == ResourceUsage::AccelerationStructureBuildOutput &&
            resource->acceleration_structure == VK_NULL_HANDLE)
        {
            // The first build creates native storage inside BuildOne. There is
            // no prior native state to transition, but the graph declaration is
            // still valid and remains the ordering authority.
            return true;
        }
        if (resource->acceleration_structure == VK_NULL_HANDLE)
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

    void VulkanAccelerationStructureOwner::DestroyRayTracingPipelineResource(
        RayTracingPipelineResource &resource) noexcept
    {
        if (!resource.alive) return;
        if (resource.pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, resource.pipeline, nullptr);
        if (resource.layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, resource.layout, nullptr);
        for (const auto &descriptor_layout : resource.descriptor_set_layouts)
        {
            if (descriptor_layout.layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device_, descriptor_layout.layout, nullptr);
        }
        if (resource.shader_binding_table.IsValid())
            buffer_manager_->DestroyBufferResource(device_, resource.shader_binding_table);
        resource = {};
    }

    void VulkanAccelerationStructureOwner::DestroyRayTracingDescriptorSet(
        RayTracingDescriptorSetResource &resource) noexcept
    {
        if (resource.alive && resource.pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, resource.pool, nullptr);
        resource = {};
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
        for (uint32_t index = 0; index < ray_tracing_descriptor_sets_.size(); ++index)
        {
            auto &resource = ray_tracing_descriptor_sets_[index];
            if (resource.alive && resource.pending_destroy && resource.retire_serial != 0 &&
                resource.retire_serial <= completed_submission_serial)
            {
                const DescriptorSetHandle handle = resource.handle;
                DestroyRayTracingDescriptorSet(resource);
                ray_tracing_descriptor_set_handle_system_.Destroy(handle);
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
        for (RayTracingDescriptorSetResource &resource : ray_tracing_descriptor_sets_)
        {
            if (resource.alive && resource.pending_destroy && resource.retire_serial == 0)
                resource.retire_serial = submission_serial;
        }
    }

    void VulkanAccelerationStructureOwner::DestroyAll() noexcept
    {
        for (RayTracingDescriptorSetResource &resource : ray_tracing_descriptor_sets_)
        {
            DestroyRayTracingDescriptorSet(resource);
        }
        for (RayTracingPipelineResource &resource : ray_tracing_pipelines_)
        {
            DestroyRayTracingPipelineResource(resource);
        }
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
