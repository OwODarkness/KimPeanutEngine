#ifndef KPENGINE_RUNTIME_GRAPHICS_RAY_TRACING_H
#define KPENGINE_RUNTIME_GRAPHICS_RAY_TRACING_H

#include <array>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "api.h"
#include "descriptor_types.h"
#include "resource_binding.h"

namespace kpengine::data
{
    struct ShaderData;
}

namespace kpengine::graphics
{
    enum class RayTracingAccelerationStructureType : uint8_t
    {
        BottomLevel,
        TopLevel,
    };

    enum class RayTracingBuildMode : uint8_t
    {
        Build,
        Update,
    };

    enum class RayTracingIndexType : uint8_t
    {
        UInt16,
        UInt32,
    };

    // The common contract accepts triangle geometry only. Vulkan geometry flags,
    // device addresses, build flags, and scratch allocations stay backend-owned.
    struct RayTracingGeometryDesc
    {
        BufferHandle vertex_buffer;
        uint64_t vertex_offset = 0;
        uint32_t vertex_stride = 0;
        uint32_t vertex_count = 0;
        BufferHandle index_buffer;
        uint64_t index_offset = 0;
        uint32_t index_count = 0;
        RayTracingIndexType index_type = RayTracingIndexType::UInt32;
    };

    struct RayTracingAccelerationStructureDesc
    {
        RayTracingAccelerationStructureType type =
            RayTracingAccelerationStructureType::BottomLevel;
        uint32_t max_geometry_count = 0;
        uint32_t max_instance_count = 0;
        bool allow_update = false;
    };

    struct RayTracingInstanceDesc
    {
        AccelerationStructureHandle bottom_level;
        // Row-major 3x4 affine transform: three rows of four floats.
        std::array<float, 12> transform{1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};
        uint32_t instance_id = 0;
        uint8_t visibility_mask = 0xFF;
    };

    struct RayTracingBuildDesc
    {
        AccelerationStructureHandle target;
        RayTracingBuildMode mode = RayTracingBuildMode::Build;
        std::span<const RayTracingGeometryDesc> geometries;
        std::span<const RayTracingInstanceDesc> instances;
    };

    // The first consumer is ray-query based, but the contract reserves the
    // smallest complete RT-pipeline description for a later dispatch path.
    struct RayTracingPipelineDesc
    {
        data::ShaderData *ray_generation_shader = nullptr;
        data::ShaderData *miss_shader = nullptr;
        data::ShaderData *closest_hit_shader = nullptr;
        uint32_t max_recursion_depth = 1;
        std::vector<std::vector<DescriptorBindingDesc>> descriptor_binding_descs;
    };

    struct RayTracingAccelerationStructureBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        AccelerationStructureHandle acceleration_structure;
    };

    struct RayTracingStorageTextureBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        TextureHandle texture;
    };

    using RayTracingResourceBinding = std::variant<
        UniformBufferBinding,
        SampledTextureBinding,
        RayTracingAccelerationStructureBinding,
        RayTracingStorageTextureBinding>;

    struct RayTracingResourceBindingSetDesc
    {
        uint32_t set = 0;
        std::vector<RayTracingResourceBinding> bindings;
        bool persistent = false;
    };

    struct RayTracingDispatchDesc
    {
        RayTracingPipelineHandle pipeline;
        DescriptorSetHandle bindings;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
    };

    constexpr bool IsRayTracingAccelerationStructureDescValid(
        const RayTracingAccelerationStructureDesc &desc) noexcept
    {
        if (desc.type == RayTracingAccelerationStructureType::BottomLevel)
        {
            return desc.max_geometry_count > 0 && desc.max_instance_count == 0;
        }
        return desc.max_geometry_count == 0 && desc.max_instance_count > 0;
    }

    inline bool IsRayTracingGeometryDescValid(
        const RayTracingGeometryDesc &geometry) noexcept
    {
        return geometry.vertex_buffer.IsValid() && geometry.index_buffer.IsValid() &&
               geometry.vertex_stride >= sizeof(float) * 3 && geometry.vertex_count >= 3 &&
               geometry.index_count >= 3;
    }

    inline bool IsRayTracingInstanceDescValid(
        const RayTracingInstanceDesc &instance) noexcept
    {
        return instance.bottom_level.IsValid() && instance.visibility_mask != 0;
    }

    inline bool IsRayTracingBuildDescValid(const RayTracingBuildDesc &desc) noexcept
    {
        if (!desc.target.IsValid() || desc.geometries.empty() == desc.instances.empty())
        {
            return false;
        }
        for (const RayTracingGeometryDesc &geometry : desc.geometries)
        {
            if (!IsRayTracingGeometryDescValid(geometry))
            {
                return false;
            }
        }
        for (const RayTracingInstanceDesc &instance : desc.instances)
        {
            if (!IsRayTracingInstanceDescValid(instance))
            {
                return false;
            }
        }
        return true;
    }

    constexpr bool IsRayTracingPipelineDescValid(
        const RayTracingPipelineDesc &desc) noexcept
    {
        return desc.ray_generation_shader != nullptr && desc.miss_shader != nullptr &&
               desc.closest_hit_shader != nullptr && desc.max_recursion_depth > 0 &&
               desc.max_recursion_depth <= 31;
    }

    inline bool IsRayTracingDispatchDescValid(
        const RayTracingDispatchDesc &desc) noexcept
    {
        return desc.pipeline.IsValid() && desc.bindings.IsValid() && desc.width > 0 &&
               desc.height > 0 && desc.depth > 0;
    }

    // Graphics owns native AS storage, RT pipelines, descriptor resources,
    // scratch reuse, and deferred destruction. Render receives only opaque
    // handles through the backend frame seam; ownership operations remain here.
    class RayTracingResourceOwner
    {
    public:
        virtual ~RayTracingResourceOwner() = default;
        virtual bool IsSupported() const noexcept = 0;
        virtual AccelerationStructureHandle CreateAccelerationStructure(
            const RayTracingAccelerationStructureDesc &desc) = 0;
        virtual bool DestroyAccelerationStructure(AccelerationStructureHandle handle) = 0;
        virtual RayTracingPipelineHandle CreateRayTracingPipeline(
            const RayTracingPipelineDesc &desc) = 0;
        virtual bool DestroyRayTracingPipeline(RayTracingPipelineHandle handle) = 0;
        virtual DescriptorSetHandle CreateRayTracingResourceBindingSet(
            RayTracingPipelineHandle pipeline,
            const RayTracingResourceBindingSetDesc &desc) = 0;
        virtual bool DestroyRayTracingResourceBindingSet(DescriptorSetHandle handle) = 0;
        // Called at submission boundaries so retired resources are not reused
        // while an in-flight frame still references them.
        virtual void CollectCompleted(uint64_t completed_submission_serial) = 0;
        virtual void RetireSubmitted(uint64_t submission_serial) = 0;
    };
}

#endif
