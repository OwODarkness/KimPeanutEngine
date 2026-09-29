#include "scene_draw_recorder.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include "asset/mesh.h"
#include "render/render_resource_resolver.h"

namespace kpengine::render
{
    namespace
    {
        struct alignas(16) SelectionGpuData
        {
            Vector4f selected{};
        };

        uint64_t GetObjectUniformKey(RenderableHandle handle)
        {
            return (uint64_t{0x4b01} << 48u) |
                   (static_cast<uint64_t>(handle.id) << 16u) | handle.generation;
        }

        uint64_t GetSelectionUniformKey(RenderableHandle handle)
        {
            return (uint64_t{0x4b02} << 48u) |
                   (static_cast<uint64_t>(handle.id) << 16u) | handle.generation;
        }
    }

    std::size_t SceneDrawRecorder::FrameObjectStateKeyHash::operator()(
        const FrameObjectStateKey &key) const noexcept
    {
        std::size_t seed = std::hash<uint32_t>{}(key.renderable.id);
        seed ^= std::hash<uint16_t>{}(key.renderable.generation) + 0x9e3779b9u +
                (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<uint8_t>{}(static_cast<uint8_t>(key.pass)) + 0x9e3779b9u +
                (seed << 6u) + (seed >> 2u);
        return seed;
    }

    void SceneDrawRecorder::BeginFrame(std::vector<MeshProxy> snapshot,
                                       uint64_t world_revision)
    {
        if (world_revision != section_packets_world_revision_)
        {
            section_packets_world_revision_ = world_revision;
            section_packets_ready_ = false;
        }
        snapshot_ = std::move(snapshot);
        frame_object_states_.clear();
        frame_material_bindings_.clear();
        profile_counters_ = {};
    }

    std::size_t SceneDrawRecorder::FrameMaterialBindingKeyHash::operator()(
        const FrameMaterialBindingKey &key) const noexcept
    {
        std::size_t seed = 0;
        const auto combine = [&seed](std::size_t value) {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };
        combine(std::hash<uint32_t>{}(key.renderable.id));
        combine(std::hash<uint32_t>{}(key.renderable.generation));
        combine(std::hash<uint32_t>{}(key.material.id));
        combine(std::hash<uint32_t>{}(key.material.generation));
        combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.pass)));
        combine(std::hash<uint32_t>{}(key.per_pass_buffer.id));
        combine(std::hash<uint32_t>{}(key.per_pass_buffer.generation));
        combine(std::hash<std::size_t>{}(key.per_pass_offset));
        combine(std::hash<std::size_t>{}(key.per_pass_range));
        return seed;
    }

    void SceneDrawRecorder::Clear()
    {
        snapshot_.clear();
        section_packets_.clear();
        section_packets_world_revision_ = 0;
        section_packets_ready_ = false;
        frame_object_states_.clear();
        frame_material_bindings_.clear();
        profile_counters_ = {};
    }

    const std::vector<VisibleMeshSection> &SceneDrawRecorder::BuildSectionCandidates(
        const RenderResourceResolver &resource_resolver)
    {
        if (!section_packets_ready_)
        {
            const auto started = std::chrono::steady_clock::now();
            section_packets_ =
                SceneVisibility::BuildSectionCandidates(snapshot_, resource_resolver);
            profile_counters_.section_packet_build_cpu_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started)
                    .count();
            ++profile_counters_.section_packet_build_calls;
            profile_counters_.section_packets_built += section_packets_.size();
            section_packets_ready_ = true;
        }
        return section_packets_;
    }

    std::vector<VisibleMeshSection> SceneDrawRecorder::BuildVisibleSections(
        const Matrix4f &view_projection,
        const RenderResourceResolver &resource_resolver)
    {
        const auto started = std::chrono::steady_clock::now();
        const std::vector<VisibleMeshSection> &section_packets =
            BuildSectionCandidates(resource_resolver);
        std::vector<VisibleMeshSection> visible_sections =
            SceneVisibility::FilterVisibleSections(view_projection, section_packets);
        profile_counters_.section_packet_build_cpu_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        ++profile_counters_.section_packet_build_calls;
        profile_counters_.section_packets_built += visible_sections.size();
        return visible_sections;
    }

    uint64_t SceneDrawRecorder::DrawMeshSections(
        const RenderResourceResolver &resource_resolver,
        graphics::CommandRecorder &recorder, graphics::MeshHandle mesh,
        uint32_t section_index) const
    {
        uint64_t draw_count = 0;
        const std::vector<data::MeshSection> *const sections =
            resource_resolver.FindMeshSections(mesh);
        if (sections == nullptr || sections->empty() ||
            section_index == std::numeric_limits<uint32_t>::max())
        {
            if (section_index == std::numeric_limits<uint32_t>::max())
            {
                if (sections == nullptr || sections->empty())
                {
                    recorder.DrawIndexed();
                    return 1;
                }
                for (const data::MeshSection &section : *sections)
                {
                    if (section.index_count != 0)
                    {
                        recorder.DrawIndexed(section.index_count, 1,
                                             section.index_start);
                        ++draw_count;
                    }
                }
            }
            return draw_count;
        }

        if (section_index < sections->size())
        {
            const data::MeshSection &section = (*sections)[section_index];
            if (section.index_count != 0)
            {
                recorder.DrawIndexed(section.index_count, 1, section.index_start);
                ++draw_count;
            }
        }
        return draw_count;
    }

    SceneDrawRecordResult SceneDrawRecorder::RecordShadowCaster(
        const MeshProxy &proxy, const UniformAllocation &per_pass,
        graphics::PipelineHandle pipeline, FrameContext &frame_context,
        const RenderResourceResolver &resource_resolver,
        graphics::CommandRecorder &recorder, uint32_t section_index)
    {
        if (!proxy.flags.visible || !proxy.flags.casts_shadow || !proxy.mesh.IsValid() ||
            !pipeline.IsValid())
        {
            return {};
        }

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_object =
            frame_context.UpdateStableUniform(GetObjectUniformKey(proxy.handle),
                                              per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return {};
        }

        const std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset,
                                           per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset,
                                           per_object.range}};
        const FrameResourceBinding resource_binding = frame_context.CreateOrGetStableBindingSet(
            0x534841444f575f42ull, pipeline, draw_bindings);
        if (!resource_binding.IsValid())
        {
            return {};
        }

        recorder.BindPipeline(pipeline);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(pipeline, resource_binding.descriptor_set,
                                      resource_binding.dynamic_offsets);
        const uint64_t draw_count = DrawMeshSections(
            resource_resolver, recorder, proxy.mesh, section_index);
        return {true, draw_count, draw_count};
    }

    SceneDrawRecordResult SceneDrawRecorder::RecordMeshProxy(
        const MeshProxy &proxy, const UniformAllocation &per_pass,
        FrameContext &frame_context, MaterialSystem &materials,
        const RenderResourceResolver &resource_resolver,
        graphics::CommandRecorder &recorder, MaterialPass pass,
        uint32_t section_index)
    {
        if (!proxy.flags.visible || !proxy.mesh.IsValid())
        {
            return {};
        }

        const FrameObjectStateKey object_key{proxy.handle, pass};
        auto object_it = frame_object_states_.find(object_key);
        if (object_it == frame_object_states_.end())
        {
            FrameObjectState state{};
            graphics::PerObjectData per_object_data{};
            per_object_data.model =
                Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
            state.per_object = frame_context.UpdateStableUniform(
                GetObjectUniformKey(proxy.handle), per_object_data);
            if (pass == MaterialPass::GBuffer)
            {
                const SelectionGpuData selection_value{
                    {proxy.flags.selected ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}};
                state.selection = frame_context.UpdateStableUniform(
                    GetSelectionUniformKey(proxy.handle), selection_value);
            }
            object_it = frame_object_states_.emplace(object_key, state).first;
        }
        const FrameObjectState &object_state = object_it->second;
        if (!per_pass.IsValid() || !object_state.per_object.IsValid())
        {
            return {};
        }

        const FrameMaterialBindingKey binding_key{
            proxy.handle, proxy.material, pass, per_pass.buffer,
            per_pass.offset, per_pass.range};
        auto binding_it = frame_material_bindings_.find(binding_key);
        if (binding_it == frame_material_bindings_.end())
        {
            std::vector<graphics::ResourceBinding> draw_bindings{
                graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset,
                                               per_pass.range},
                graphics::UniformBufferBinding{0, 1, object_state.per_object.buffer,
                                               object_state.per_object.offset,
                                               object_state.per_object.range}};
            if (pass == MaterialPass::GBuffer)
            {
                if (!object_state.selection.IsValid())
                {
                    return {};
                }
                draw_bindings.emplace_back(graphics::UniformBufferBinding{
                    0, 9, object_state.selection.buffer, object_state.selection.offset,
                    object_state.selection.range});
            }
            const FrameMaterialBinding resolved = frame_context.CreateMaterialBinding(
                materials, resource_resolver, proxy.material, draw_bindings, pass);
            binding_it = frame_material_bindings_.emplace(binding_key, resolved).first;
        }
        const FrameMaterialBinding &material_binding = binding_it->second;
        if (!frame_context.IsMaterialBindingCurrent(material_binding))
        {
            return {};
        }

        recorder.BindPipeline(material_binding.pipeline);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(material_binding.pipeline,
                                      material_binding.descriptor_set,
                                      material_binding.dynamic_offsets);
        const uint64_t draw_count = DrawMeshSections(
            resource_resolver, recorder, proxy.mesh, section_index);
        return {true, draw_count, draw_count};
    }
}
