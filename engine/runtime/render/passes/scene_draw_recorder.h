#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SCENE_DRAW_RECORDER_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SCENE_DRAW_RECORDER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include "render/frame_context.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "render/material/material_system.h"
#include "render/render_world/scene_visibility.h"

namespace kpengine::render
{
    class RenderResourceResolver;

    struct SceneDrawRecordResult
    {
        bool succeeded = false;
        uint64_t draw_calls = 0;
        uint64_t sections = 0;
    };

    struct SceneDrawProfileCounters
    {
        double section_packet_build_cpu_ms = 0.0;
        uint64_t section_packet_build_calls = 0;
        uint64_t section_packets_built = 0;
    };

    // Shares one revision-stable section snapshot and frame-local binding cache
    // across shadow and GBuffer recording without owning frame or GPU resources.
    class SceneDrawRecorder final
    {
    public:
        void BeginFrame(std::vector<MeshProxy> snapshot, uint64_t world_revision);
        void Clear();

        const std::vector<MeshProxy> &Snapshot() const noexcept { return snapshot_; }
        const std::vector<VisibleMeshSection> &BuildSectionCandidates(
            const RenderResourceResolver &resource_resolver);
        std::vector<VisibleMeshSection> BuildVisibleSections(
            const Matrix4f &view_projection,
            const RenderResourceResolver &resource_resolver);
        const SceneDrawProfileCounters &GetProfileCounters() const noexcept
        {
            return profile_counters_;
        }

        SceneDrawRecordResult RecordShadowCaster(
            const MeshProxy &proxy, const UniformAllocation &per_pass,
            graphics::PipelineHandle pipeline, FrameContext &frame_context,
            const RenderResourceResolver &resource_resolver,
            graphics::CommandRecorder &recorder,
            uint32_t section_index = std::numeric_limits<uint32_t>::max());
        SceneDrawRecordResult RecordMeshProxy(
            const MeshProxy &proxy, const UniformAllocation &per_pass,
            FrameContext &frame_context, MaterialSystem &materials,
            const RenderResourceResolver &resource_resolver,
            graphics::CommandRecorder &recorder, MaterialPass pass,
            uint32_t section_index = std::numeric_limits<uint32_t>::max());

    private:
        struct FrameObjectStateKey
        {
            RenderableHandle renderable;
            MaterialPass pass = MaterialPass::Scene;

            friend bool operator==(const FrameObjectStateKey &,
                                   const FrameObjectStateKey &) = default;
        };

        struct FrameObjectStateKeyHash
        {
            std::size_t operator()(const FrameObjectStateKey &key) const noexcept;
        };

        struct FrameObjectState
        {
            UniformAllocation per_object;
            UniformAllocation selection;
        };

        struct FrameMaterialBindingKey
        {
            RenderableHandle renderable;
            MaterialInstanceHandle material;
            MaterialPass pass = MaterialPass::Scene;
            graphics::BufferHandle per_pass_buffer;
            std::size_t per_pass_offset = 0;
            std::size_t per_pass_range = 0;

            friend bool operator==(const FrameMaterialBindingKey &,
                                   const FrameMaterialBindingKey &) = default;
        };

        struct FrameMaterialBindingKeyHash
        {
            std::size_t operator()(const FrameMaterialBindingKey &key) const noexcept;
        };

        uint64_t DrawMeshSections(const RenderResourceResolver &resource_resolver,
                                  graphics::CommandRecorder &recorder,
                                  graphics::MeshHandle mesh,
                                  uint32_t section_index) const;

        std::vector<MeshProxy> snapshot_;
        std::vector<VisibleMeshSection> section_packets_;
        uint64_t section_packets_world_revision_ = 0;
        bool section_packets_ready_ = false;
        std::unordered_map<FrameObjectStateKey, FrameObjectState,
                           FrameObjectStateKeyHash> frame_object_states_;
        std::unordered_map<FrameMaterialBindingKey, FrameMaterialBinding,
                           FrameMaterialBindingKeyHash> frame_material_bindings_;
        SceneDrawProfileCounters profile_counters_{};
    };
}

#endif
