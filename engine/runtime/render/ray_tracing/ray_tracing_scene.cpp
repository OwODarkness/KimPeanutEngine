#include "ray_tracing_scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>

#include "asset/texture.h"
#include "asset/mesh.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "render/material/material_asset_resolver.h"
#include "render/material/material_system.h"
#include "render/passes/scene_draw_recorder.h"
#include "render/render_graph/render_graph_bindings.h"
#include "render/render_graph/render_graph_executor.h"
#include "render/render_pass_declaration.h"
#include "render/render_resource_resolver.h"
#include "render/render_world/render_world.h"
#include "render/ray_tracing_scene_sections.h"
#include "render/render_world/scene_draw_list.h"
#include "render/ray_tracing/path_tracing_scene_data.h"

namespace kpengine::render
{
    namespace
    {
        template <typename T, typename Range>
        bool ContainsAll(std::span<const T> available, const Range &required)
        {
            return std::all_of(required.begin(), required.end(), [&](const T &value) {
                return std::find(available.begin(), available.end(), value) != available.end();
            });
        }
    }

    bool RayTracingScene::Prepare(const RayTracingScenePrepareContext &context)
    {
        graphics::RenderBackend *const backend_ = &context.backend;
        const RenderWorld *const render_world_ = context.world;
        const SceneDrawRecorder &scene_draw_recorder_ = context.draw_recorder;
        MaterialSystem *const material_system_ = context.materials;
        RenderResourceResolver *const resource_resolver_ = context.resources;
        const bool path_tracing_enabled_ = context.path_tracing_enabled;
        graphics::RayTracingResourceOwner *const owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        if (owner != nullptr)
        {
            if (frame_ray_tracing_blas_resources_.has_value())
                owner->CancelPreparedBuildResources(frame_ray_tracing_blas_resources_->token);
            if (frame_ray_tracing_tlas_resources_.has_value())
                owner->CancelPreparedBuildResources(frame_ray_tracing_tlas_resources_->token);
        }
        frame_ray_tracing_blas_resources_.reset();
        frame_ray_tracing_tlas_resources_.reset();
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_lighting_signature_ = 0;
        frame_ray_tracing_blas_builds_.clear();
        frame_ray_tracing_tlas_builds_.clear();
        frame_ray_tracing_blas_build_ = false;
        frame_ray_tracing_tlas_build_ = false;
        RebuildFrameViews();

        if (owner == nullptr || !owner->IsSupported())
        {
            ray_tracing_scene_cache_valid_ = false;
            frame_ray_tracing_scene_table_dirty_ = true;
            frame_ray_tracing_geometries_.clear();
            frame_ray_tracing_instances_.clear();
            frame_ray_tracing_instance_data_.clear();
            frame_ray_tracing_material_data_.clear();
            frame_ray_tracing_mesh_builds_.clear();
            RebuildFrameViews();
            return true;
        }

        const uint64_t world_revision = render_world_ != nullptr
                                            ? render_world_->GetRevision()
                                            : 0;
        const uint64_t material_revision = material_system_ != nullptr
                                               ? material_system_->GetRevision()
                                               : 0;
        const bool scene_cache_dirty = !ray_tracing_scene_cache_valid_ ||
            world_revision != ray_tracing_scene_cache_world_revision_ ||
            material_revision != ray_tracing_scene_cache_material_revision_ ||
            path_tracing_enabled_ != ray_tracing_scene_cache_path_tracing_enabled_;
        if (scene_cache_dirty)
        {
            ++ray_tracing_scene_record_cache_misses_total_;
        }
        else
        {
            ++ray_tracing_scene_record_cache_hits_total_;
        }
        frame_ray_tracing_scene_table_dirty_ = scene_cache_dirty;
        bool scene_records_complete = true;
        if (scene_cache_dirty)
        {
            frame_ray_tracing_geometries_.clear();
            frame_ray_tracing_instances_.clear();
            frame_ray_tracing_instance_data_.clear();
            frame_ray_tracing_material_data_.clear();
            frame_ray_tracing_mesh_builds_.clear();
        }
        else
        {
            frame_ray_tracing_instance_signature_ =
                ray_tracing_scene_cache_instance_signature_;
            frame_ray_tracing_material_signature_ =
                ray_tracing_scene_cache_material_signature_;
            for (MeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
            {
                const auto state = ray_tracing_blas_.find(mesh_build.key);
                mesh_build.needs_build = state != ray_tracing_blas_.end() &&
                                         !state->second.built;
            }
        }

        uint64_t instance_signature = 1469598103934665603ull;
        uint64_t material_signature = 1469598103934665603ull;
        const auto add_signature = [&instance_signature](uint64_t value) {
            instance_signature ^= value;
            instance_signature *= 1099511628211ull;
        };
        const auto add_material_signature = [&material_signature](uint64_t value) {
            material_signature ^= value;
            material_signature *= 1099511628211ull;
        };
        const auto add_float = [&add_signature](float value) {
            add_signature(static_cast<uint64_t>(std::hash<float>{}(value)));
        };

        if (scene_cache_dirty)
        {
            std::unordered_map<detail::RayTracingSectionKey, std::size_t,
                               detail::RayTracingSectionKeyHash> mesh_indices;
            std::unordered_map<graphics::MeshHandle,
                               std::vector<graphics::RayTracingGeometryDesc>> mesh_geometries;
            for (const MeshProxy &proxy : scene_draw_recorder_.Snapshot())
            {
                if (!proxy.flags.visible || !proxy.mesh.IsValid())
                {
                    continue;
                }

                auto [geometry_iterator, first_mesh_use] = mesh_geometries.try_emplace(proxy.mesh);
                if (first_mesh_use)
                    geometry_iterator->second = backend_->GetRayTracingGeometry(proxy.mesh);
                const auto &source_geometries = geometry_iterator->second;
                const std::vector<data::MeshSection> *const sections =
                    resource_resolver_ != nullptr
                        ? resource_resolver_->FindMeshSections(proxy.mesh)
                        : nullptr;
                std::vector<std::optional<MaterialDrawClass>> draw_classes;
                draw_classes.reserve(source_geometries.size());
                for (std::size_t section = 0; section < source_geometries.size(); ++section)
                {
                    const MaterialInstanceHandle material =
                        sections != nullptr && section < sections->size()
                            ? proxy.GetMaterialForSection((*sections)[section].material_index)
                            : proxy.material;
                    draw_classes.push_back(material_system_ != nullptr
                                               ? material_system_->GetDrawClass(material)
                                               : std::nullopt);
                }
                const detail::RayTracingSectionKey section_key =
                    detail::MakeOpaqueRayTracingSectionKey(proxy.mesh, draw_classes);
                if (section_key.section_indices.empty())
                    continue;

                const auto [mesh_iterator, inserted] = mesh_indices.emplace(
                    section_key, frame_ray_tracing_mesh_builds_.size());
                if (inserted)
                {
                    std::vector<graphics::RayTracingGeometryDesc> geometries;
                    geometries.reserve(section_key.section_indices.size());
                    for (uint32_t section : section_key.section_indices)
                        geometries.push_back(source_geometries[section]);

                    const uint64_t geometry_signature =
                        detail::RayTracingGeometrySignature(geometries);
                    BlasState &state = ray_tracing_blas_[section_key];
                    if (state.handle.IsValid() &&
                        (state.geometry_count != geometries.size() ||
                         state.geometry_signature != geometry_signature))
                    {
                        owner->DestroyAccelerationStructure(state.handle);
                        state = {};
                    }
                    if (!state.handle.IsValid())
                    {
                        state.handle = owner->CreateAccelerationStructure(
                            {graphics::RayTracingAccelerationStructureType::BottomLevel,
                             static_cast<uint32_t>(geometries.size()), 0, false});
                        state.geometry_count = static_cast<uint32_t>(geometries.size());
                        state.geometry_signature = geometry_signature;
                        state.built = false;
                    }
                    if (!state.handle.IsValid())
                    {
                        scene_records_complete = false;
                        mesh_indices.erase(mesh_iterator);
                        continue;
                    }

                    const std::size_t geometry_offset = frame_ray_tracing_geometries_.size();
                    frame_ray_tracing_geometries_.insert(frame_ray_tracing_geometries_.end(),
                                                         geometries.begin(), geometries.end());
                    frame_ray_tracing_mesh_builds_.push_back(
                        {section_key, state.handle, geometry_offset, geometries.size(),
                         geometry_signature, !state.built});
                    if (!state.built)
                    {
                        frame_ray_tracing_blas_build_ = true;
                    }
                }

                const auto build_iterator = mesh_indices.find(section_key);
                if (build_iterator == mesh_indices.end())
                {
                    continue;
                }
                const MeshBuild &mesh_build =
                    frame_ray_tracing_mesh_builds_[build_iterator->second];
                graphics::RayTracingInstanceDesc instance{};
                instance.bottom_level = mesh_build.blas;
                instance.instance_id = static_cast<uint32_t>(frame_ray_tracing_instances_.size());
                const Matrix4f transform = Matrix4f::MakeTransformMatrix(proxy.world_transform);
                for (std::size_t row = 0; row < 3; ++row)
                {
                    for (std::size_t column = 0; column < 4; ++column)
                    {
                        instance.transform[row * 4 + column] = transform[row][column];
                        add_float(transform[row][column]);
                    }
                }
                add_signature(proxy.handle.id);
                add_signature(proxy.handle.generation);
                add_signature(proxy.mesh.id);
                add_signature(proxy.mesh.generation);
                add_signature(mesh_build.geometry_signature);
                add_signature(mesh_build.blas.id);
                add_signature(mesh_build.blas.generation);
                if (path_tracing_enabled_)
                {
                    const std::size_t material_offset = frame_ray_tracing_material_data_.size();
                    for (std::size_t geometry = 0; geometry < mesh_build.geometry_count; ++geometry)
                    {
                        ray_tracing::PathTracingMaterialRecord material_data{};
                        MaterialInstanceHandle material = proxy.material;
                        const uint32_t section = mesh_build.key.section_indices[geometry];
                        if (sections != nullptr && section < sections->size())
                        {
                            material = proxy.GetMaterialForSection((*sections)[section].material_index);
                        }
                        if (material_system_ != nullptr && material.IsValid())
                        {
                            const MaterialTemplateHandle template_handle =
                                material_system_->GetInstanceTemplate(material);
                            const auto read_parameter = [material_system_, material, template_handle](
                                std::string_view name) -> const MaterialParameterValue * {
                                const MaterialParameterID parameter =
                                    material_system_->FindParameterID(template_handle, name);
                                return material_system_->GetParameterValue(material, parameter);
                            };
                            if (const MaterialParameterValue *value = read_parameter("base_color"))
                            {
                                if (const Vector4f *color = std::get_if<Vector4f>(value))
                                {
                                    material_data.base_color = *color;
                                }
                            }
                            if (const MaterialParameterValue *value = read_parameter("emissive"))
                            {
                                if (const Vector4f *emissive = std::get_if<Vector4f>(value))
                                {
                                    material_data.emissive = *emissive;
                                }
                            }
                            if (const MaterialParameterValue *value = read_parameter("metallic"))
                            {
                                if (const float *metallic = std::get_if<float>(value))
                                {
                                    material_data.metallic = *metallic;
                                }
                            }
                            if (const MaterialParameterValue *value = read_parameter("roughness"))
                            {
                                if (const float *roughness = std::get_if<float>(value))
                                {
                                    material_data.roughness = *roughness;
                                }
                            }
                            const MaterialParameterID texture_parameter =
                                material_system_->FindParameterID(template_handle, "base_color_texture");
                            if (texture_parameter.IsValid() && resource_resolver_ != nullptr)
                            {
                                if (const MaterialParameterValue *texture_value =
                                        material_system_->GetParameterValue(material, texture_parameter))
                                {
                                    if (const auto *texture =
                                            std::get_if<MaterialTextureSamplerValue>(texture_value))
                                    {
                                        add_material_signature(texture->texture_asset.Pack());
                                    }
                                }
                                if (const auto *textures =
                                        resource_resolver_->FindTextureBindings(material))
                                {
                                    const auto slot = textures->ray_tracing_bindless_slots.find(
                                        texture_parameter.value);
                                    if (slot != textures->ray_tracing_bindless_slots.end() &&
                                        slot->second.IsValid())
                                    {
                                        material_data.base_color_texture_index = slot->second.id;
                                    }
                                }
                            }
                            const auto resolve_scalar_texture = [&](std::string_view name) {
                                const MaterialParameterID parameter =
                                    material_system_->FindParameterID(template_handle, name);
                                uint32_t index = 0xffffffffu;
                                if (parameter.IsValid() && resource_resolver_ != nullptr)
                                {
                                    if (const auto *textures =
                                            resource_resolver_->FindTextureBindings(material))
                                    {
                                        const auto slot =
                                            textures->ray_tracing_bindless_slots.find(parameter.value);
                                        if (slot != textures->ray_tracing_bindless_slots.end() &&
                                            slot->second.IsValid())
                                        {
                                            index = slot->second.id;
                                        }
                                    }
                                    if (const auto *value =
                                            material_system_->GetParameterValue(material, parameter))
                                    {
                                        if (const auto *texture =
                                                std::get_if<MaterialTextureSamplerValue>(value))
                                        {
                                            add_material_signature(texture->texture_asset.Pack());
                                        }
                                    }
                                }
                                return index;
                            };
                            material_data.metallic_texture_index =
                                resolve_scalar_texture("metallic_texture");
                            material_data.roughness_texture_index =
                                resolve_scalar_texture("roughness_texture");
                            if (const auto *value = read_parameter("texture_channels"))
                            {
                                if (const auto *channels = std::get_if<Vector4f>(value))
                                {
                                    material_data.metallic_channel =
                                        static_cast<uint32_t>(std::clamp(channels->x_, 0.0f, 3.0f));
                                    material_data.roughness_channel =
                                        static_cast<uint32_t>(std::clamp(channels->y_, 0.0f, 3.0f));
                                }
                            }
                            add_material_signature(material.id);
                            add_material_signature(material.generation);
                            add_material_signature(material_system_->GetInstanceRevision(material));
                        }
                    const auto add_material_color = [&add_material_signature](float component) {
                        add_material_signature(static_cast<uint64_t>(std::hash<float>{}(component)));
                    };
                    add_material_color(material_data.base_color.x_);
                    add_material_color(material_data.base_color.y_);
                    add_material_color(material_data.base_color.z_);
                    add_material_color(material_data.emissive.x_);
                    add_material_color(material_data.emissive.y_);
                    add_material_color(material_data.emissive.z_);
                    add_material_color(material_data.metallic);
                    add_material_color(material_data.roughness);
                    add_material_signature(material_data.base_color_texture_index);
                    add_material_signature(material_data.metallic_texture_index);
                    add_material_signature(material_data.roughness_texture_index);
                    add_material_signature(material_data.metallic_channel);
                    add_material_signature(material_data.roughness_channel);
                    frame_ray_tracing_material_data_.push_back(material_data);
                }
                    frame_ray_tracing_instance_data_.push_back(
                        {static_cast<uint32_t>(mesh_build.geometry_offset),
                         static_cast<uint32_t>(material_offset),
                         static_cast<uint32_t>(mesh_build.geometry_count)});
                }
                frame_ray_tracing_instances_.push_back(instance);
            }

            frame_ray_tracing_instance_signature_ = instance_signature;
            frame_ray_tracing_material_signature_ = material_signature;
            ray_tracing_scene_cache_world_revision_ = world_revision;
            ray_tracing_scene_cache_material_revision_ = material_revision;
            ray_tracing_scene_cache_instance_signature_ = instance_signature;
            ray_tracing_scene_cache_material_signature_ = material_signature;
            ray_tracing_scene_cache_path_tracing_enabled_ = path_tracing_enabled_;
            ray_tracing_scene_cache_valid_ = scene_records_complete;
        }

        if (frame_ray_tracing_instances_.empty())
        {
            RebuildFrameViews();
            return true;
        }

        if (!ray_tracing_tlas_.IsValid() ||
            ray_tracing_tlas_capacity_ < frame_ray_tracing_instances_.size())
        {
            if (ray_tracing_tlas_.IsValid())
            {
                owner->DestroyAccelerationStructure(ray_tracing_tlas_);
            }
            ray_tracing_tlas_ = owner->CreateAccelerationStructure(
                {graphics::RayTracingAccelerationStructureType::TopLevel, 0,
                 static_cast<uint32_t>(frame_ray_tracing_instances_.size()), true});
            ray_tracing_tlas_capacity_ =
                static_cast<uint32_t>(frame_ray_tracing_instances_.size());
            ray_tracing_tlas_built_ = false;
        }
        if (!ray_tracing_tlas_.IsValid())
        {
            RebuildFrameViews();
            return true;
        }

        for (const MeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
        {
            if (mesh_build.needs_build)
            {
                frame_ray_tracing_blas_build_ = true;
                frame_ray_tracing_blas_builds_.push_back(
                    {mesh_build.blas, graphics::RayTracingBuildMode::Build,
                     std::span<const graphics::RayTracingGeometryDesc>(
                         frame_ray_tracing_geometries_.data() + mesh_build.geometry_offset,
                         mesh_build.geometry_count),
                     {}});
            }
        }

        frame_ray_tracing_tlas_build_ =
            !ray_tracing_tlas_built_ || frame_ray_tracing_instance_signature_ !=
                                            ray_tracing_instance_signature_;
        if (frame_ray_tracing_tlas_build_)
        {
            frame_ray_tracing_tlas_builds_.push_back(
                {ray_tracing_tlas_,
                 ray_tracing_tlas_built_ ? graphics::RayTracingBuildMode::Update
                                         : graphics::RayTracingBuildMode::Build,
                 {},
                     std::span<const graphics::RayTracingInstanceDesc>(
                     frame_ray_tracing_instances_.data(), frame_ray_tracing_instances_.size())});
        }
        RebuildFrameViews();
        return true;
    }

    bool RayTracingScene::RecordBlasBuild(const RenderGraphPassContext &context)
    {
        graphics::CommandRecorder &recorder = context.GetRecorder();
        const auto targets = context.ResolveAccelerationStructures(
            RenderFrameResourceRole::SceneBlas, RenderGraphAccess::Write);
        const auto geometry = context.ResolveBuffers(
            RenderFrameResourceRole::SceneGeometry, RenderGraphAccess::Read);
        const auto scratch = context.ResolveBuffers(
            RenderFrameResourceRole::SceneScratch, RenderGraphAccess::Write);
        if (frame_ray_tracing_blas_builds_.empty() ||
            !frame_ray_tracing_blas_resources_.has_value() ||
            frame_ray_tracing_blas_resources_->scratch_buffers.size() !=
                frame_ray_tracing_blas_builds_.size() ||
            !std::all_of(frame_ray_tracing_blas_builds_.begin(),
                         frame_ray_tracing_blas_builds_.end(), [&](const auto &build) {
                return std::find(targets.begin(), targets.end(), build.target) != targets.end();
            }) ||
            !ContainsAll<graphics::BufferHandle>(scratch,
                frame_ray_tracing_blas_resources_->scratch_buffers))
        {
            return false;
        }
        for (const graphics::RayTracingBuildDesc &build : frame_ray_tracing_blas_builds_)
        {
            if (!build.instances.empty() || build.geometries.empty())
                return false;
            for (const graphics::RayTracingGeometryDesc &item : build.geometries)
            {
                if (std::find(geometry.begin(), geometry.end(), item.vertex_buffer) == geometry.end() ||
                    std::find(geometry.begin(), geometry.end(), item.index_buffer) == geometry.end())
                    return false;
            }
        }
        if (!recorder.BuildAccelerationStructures(frame_ray_tracing_blas_builds_,
                                                  *frame_ray_tracing_blas_resources_))
        {
            return false;
        }
        for (const MeshBuild &mesh_build : frame_ray_tracing_mesh_builds_)
        {
            if (mesh_build.needs_build)
            {
                const auto iterator = ray_tracing_blas_.find(mesh_build.key);
                if (iterator != ray_tracing_blas_.end())
                {
                    iterator->second.built = true;
                }
            }
        }
        return true;
    }

    bool RayTracingScene::RecordTlasBuild(const RenderGraphPassContext &context)
    {
        graphics::CommandRecorder &recorder = context.GetRecorder();
        const auto targets = context.ResolveAccelerationStructures(
            RenderFrameResourceRole::SceneTlas, RenderGraphAccess::Write);
        const auto blas = context.ResolveAccelerationStructures(
            RenderFrameResourceRole::SceneBlas, RenderGraphAccess::Read);
        const auto instances = context.ResolveBuffers(
            RenderFrameResourceRole::SceneInstances, RenderGraphAccess::Read);
        const auto scratch = context.ResolveBuffers(
            RenderFrameResourceRole::SceneScratch, RenderGraphAccess::Write);
        if (frame_ray_tracing_tlas_builds_.size() != 1 ||
            !frame_ray_tracing_tlas_resources_.has_value() ||
            frame_ray_tracing_tlas_resources_->instance_inputs.size() !=
                frame_ray_tracing_tlas_builds_.size() ||
            frame_ray_tracing_tlas_resources_->scratch_buffers.size() !=
                frame_ray_tracing_tlas_builds_.size() ||
            std::find(targets.begin(), targets.end(),
                      frame_ray_tracing_tlas_builds_.front().target) == targets.end() ||
            !ContainsAll<graphics::BufferHandle>(instances,
                frame_ray_tracing_tlas_resources_->instance_inputs) ||
            !ContainsAll<graphics::BufferHandle>(scratch,
                frame_ray_tracing_tlas_resources_->scratch_buffers))
        {
            return false;
        }
        for (const graphics::RayTracingBuildDesc &build : frame_ray_tracing_tlas_builds_)
        {
            if (!build.geometries.empty() || build.instances.empty())
                return false;
            for (const graphics::RayTracingInstanceDesc &instance : build.instances)
            {
                if (std::find(blas.begin(), blas.end(), instance.bottom_level) == blas.end())
                    return false;
            }
        }
        if (!recorder.BuildAccelerationStructures(frame_ray_tracing_tlas_builds_,
                                                   *frame_ray_tracing_tlas_resources_))
        {
            return false;
        }
        ray_tracing_tlas_built_ = true;
        ray_tracing_instance_signature_ = frame_ray_tracing_instance_signature_;
        return true;
    }

    bool RayTracingScene::PrepareBuildResources(graphics::RayTracingResourceOwner &owner,
                                                  uint64_t frame_number)
    {
        CancelPreparedBuildResources(&owner);
        if (!frame_ray_tracing_blas_builds_.empty())
        {
            frame_ray_tracing_blas_resources_ = owner.PrepareBuildResources(
                frame_ray_tracing_blas_builds_, frame_number);
            if (!frame_ray_tracing_blas_resources_.has_value())
                return false;
        }
        if (!frame_ray_tracing_tlas_builds_.empty())
        {
            frame_ray_tracing_tlas_resources_ = owner.PrepareBuildResources(
                frame_ray_tracing_tlas_builds_, frame_number);
            if (!frame_ray_tracing_tlas_resources_.has_value())
            {
                CancelPreparedBuildResources(&owner);
                return false;
            }
        }
        RebuildFrameViews();
        return true;
    }

    void RayTracingScene::CancelPreparedBuildResources(
        graphics::RayTracingResourceOwner *owner) noexcept
    {
        if (owner != nullptr)
        {
            if (frame_ray_tracing_blas_resources_.has_value())
                owner->CancelPreparedBuildResources(frame_ray_tracing_blas_resources_->token);
            if (frame_ray_tracing_tlas_resources_.has_value())
                owner->CancelPreparedBuildResources(frame_ray_tracing_tlas_resources_->token);
        }
        frame_ray_tracing_blas_resources_.reset();
        frame_ray_tracing_tlas_resources_.reset();
        frame_ray_tracing_instance_inputs_.clear();
        frame_ray_tracing_scratch_buffers_.clear();
    }

    void RayTracingScene::PrepareLights(std::span<const Light> lights,
                                         bool path_tracing_enabled)
    {
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_lighting_signature_ = 1469598103934665603ull;
        const auto add_signature = [this](float value) {
            frame_ray_tracing_lighting_signature_ ^=
                static_cast<uint64_t>(std::hash<float>{}(value));
            frame_ray_tracing_lighting_signature_ *= 1099511628211ull;
        };
        constexpr std::size_t kMaximumLights = 128;
        if (path_tracing_enabled)
        {
            for (const Light &light : lights)
            {
                if (!light.desc.enabled || frame_ray_tracing_light_data_.size() >= kMaximumLights)
                    continue;
                ray_tracing::PathTracingLightRecord record{};
                record.color_intensity = Vector4f{light.desc.color, light.desc.intensity};
                record.parameters.z_ = light.desc.shadow.has_value() ? 1.0f : 0.0f;
                if (light.desc.type == LightType::Directional)
                {
                    const auto *directional =
                        std::get_if<DirectionalLightData>(&light.desc.type_data);
                    if (directional == nullptr || directional->direction.SquareLength() <= 0.0f)
                        continue;
                    record.direction_and_range =
                        Vector4f{directional->direction.GetSafetyNormalize(), 0.0f};
                }
                else if (light.desc.type == LightType::Point)
                {
                    const auto *point = std::get_if<PointLightData>(&light.desc.type_data);
                    if (point == nullptr || !(point->range > 0.0f))
                        continue;
                    const bool rectangle = point->half_axis_u.SquareLength() > 0.0f;
                    record.position_or_type = Vector4f{point->position, rectangle ? 3.0f : 1.0f};
                    record.direction_and_range = Vector4f{point->half_axis_u, point->range};
                    if (rectangle)
                        record.parameters = Vector4f{point->half_axis_v, record.parameters.z_};
                }
                else
                {
                    const auto *spot = std::get_if<SpotLightData>(&light.desc.type_data);
                    if (spot == nullptr || !(spot->range > 0.0f) ||
                        spot->direction.SquareLength() <= 0.0f)
                        continue;
                    record.position_or_type = Vector4f{spot->position, 2.0f};
                    record.direction_and_range =
                        Vector4f{spot->direction.GetSafetyNormalize(), spot->range};
                    record.parameters.x_ = std::cos(spot->inner_cone_radians);
                    record.parameters.y_ = std::cos(spot->outer_cone_radians);
                }
                for (const float component : {
                         record.position_or_type.x_, record.position_or_type.y_,
                         record.position_or_type.z_, record.position_or_type.w_,
                         record.direction_and_range.x_, record.direction_and_range.y_,
                         record.direction_and_range.z_, record.direction_and_range.w_,
                         record.color_intensity.x_, record.color_intensity.y_,
                         record.color_intensity.z_, record.color_intensity.w_,
                         record.parameters.x_, record.parameters.y_, record.parameters.z_,
                         record.parameters.w_})
                    add_signature(component);
                frame_ray_tracing_light_data_.push_back(record);
            }
        }
        frame_ray_tracing_scene_table_dirty_ = frame_ray_tracing_scene_table_dirty_ ||
            frame_ray_tracing_lighting_signature_ != ray_tracing_scene_table_lighting_signature_;
        RebuildFrameViews();
    }

    void RayTracingScene::RebuildFrameViews()
    {
        frame_ray_tracing_blas_handles_.clear();
        frame_ray_tracing_blas_handles_.reserve(ray_tracing_blas_.size());
        for (const auto &[key, state] : ray_tracing_blas_)
        {
            (void)key;
            if (state.handle.IsValid())
                frame_ray_tracing_blas_handles_.push_back(state.handle);
        }
        frame_ray_tracing_instance_inputs_.clear();
        frame_ray_tracing_scratch_buffers_.clear();
        const auto append_resources = [this](const auto &resources) {
            if (!resources.has_value())
                return;
            frame_ray_tracing_instance_inputs_.insert(
                frame_ray_tracing_instance_inputs_.end(), resources->instance_inputs.begin(),
                resources->instance_inputs.end());
            frame_ray_tracing_scratch_buffers_.insert(
                frame_ray_tracing_scratch_buffers_.end(), resources->scratch_buffers.begin(),
                resources->scratch_buffers.end());
        };
        append_resources(frame_ray_tracing_blas_resources_);
        append_resources(frame_ray_tracing_tlas_resources_);
    }

    RayTracingSceneView RayTracingScene::View() const noexcept
    {
        return {frame_ray_tracing_geometries_, frame_ray_tracing_instances_,
                frame_ray_tracing_instance_data_, frame_ray_tracing_material_data_,
                frame_ray_tracing_light_data_, frame_ray_tracing_blas_handles_,
                frame_ray_tracing_blas_builds_, frame_ray_tracing_tlas_builds_,
                frame_ray_tracing_instance_inputs_, frame_ray_tracing_scratch_buffers_,
                ray_tracing_tlas_, ray_tracing_scene_table_,
                frame_ray_tracing_instance_signature_, frame_ray_tracing_material_signature_,
                frame_ray_tracing_lighting_signature_, ray_tracing_scene_record_cache_hits_total_,
                ray_tracing_scene_record_cache_misses_total_, frame_ray_tracing_scene_table_dirty_,
                frame_ray_tracing_blas_build_, frame_ray_tracing_tlas_build_};
    }

    const std::optional<graphics::RayTracingBuildResources> &
    RayTracingScene::BlasResources() const noexcept
    {
        return frame_ray_tracing_blas_resources_;
    }

    const std::optional<graphics::RayTracingBuildResources> &
    RayTracingScene::TlasResources() const noexcept
    {
        return frame_ray_tracing_tlas_resources_;
    }

    RayTracingSceneTableUpdate RayTracingScene::EnsureReferenceTable(
        graphics::RayTracingResourceOwner &owner,
        std::span<const graphics::BufferHandle> declared_geometry)
    {
        namespace scene_data = ray_tracing::path_trace_scene_data;
        RayTracingSceneTableUpdate update{};
        update.table = ray_tracing_scene_table_;
        if (frame_ray_tracing_geometries_.empty() ||
            frame_ray_tracing_geometries_.size() > scene_data::kMaximumSceneRecords ||
            frame_ray_tracing_instance_data_.size() > scene_data::kMaximumSceneRecords ||
            frame_ray_tracing_material_data_.size() > scene_data::kMaximumSceneRecords ||
            frame_ray_tracing_light_data_.size() > 128)
        {
            return update;
        }
        for (const graphics::RayTracingGeometryDesc &geometry : frame_ray_tracing_geometries_)
        {
            if (std::find(declared_geometry.begin(), declared_geometry.end(),
                          geometry.vertex_buffer) == declared_geometry.end() ||
                std::find(declared_geometry.begin(), declared_geometry.end(),
                          geometry.index_buffer) == declared_geometry.end())
                return update;
        }
        if (!frame_ray_tracing_scene_table_dirty_ && ray_tracing_scene_table_.IsValid())
        {
            update.succeeded = true;
            return update;
        }

        const auto pack_started = std::chrono::steady_clock::now();
        scene_data::PathTracingSceneGpuData packed{};
        frame_ray_tracing_scene_address_patches_.clear();
        frame_ray_tracing_scene_address_patches_.reserve(
            frame_ray_tracing_geometries_.size() * 2);
        for (std::size_t index = 0; index < frame_ray_tracing_geometries_.size(); ++index)
        {
            const graphics::RayTracingGeometryDesc &source = frame_ray_tracing_geometries_[index];
            if (source.index_type != graphics::RayTracingIndexType::UInt32)
                return update;
            scene_data::PathTracingGeometryGpuData &destination = packed.geometry_data[index];
            destination.words = {0u, 0u, 0u, 0u, source.vertex_stride,
                                 static_cast<uint32_t>(source.index_type),
                                 static_cast<uint32_t>(offsetof(data::Vertex, tex_coord)),
                                 static_cast<uint32_t>(offsetof(data::Vertex, normal))};
            const std::size_t address_offset =
                offsetof(scene_data::PathTracingSceneGpuData, geometry_data) +
                index * sizeof(scene_data::PathTracingGeometryGpuData) +
                offsetof(scene_data::PathTracingGeometryGpuData, words);
            frame_ray_tracing_scene_address_patches_.push_back(
                {address_offset, source.vertex_buffer, source.vertex_offset});
            frame_ray_tracing_scene_address_patches_.push_back(
                {address_offset + sizeof(uint64_t), source.index_buffer, source.index_offset});
        }
        for (std::size_t index = 0; index < frame_ray_tracing_instance_data_.size(); ++index)
        {
            const ray_tracing::PathTracingInstanceRecord &source =
                frame_ray_tracing_instance_data_[index];
            scene_data::PathTracingInstanceGpuData &destination = packed.instance_data[index];
            destination.geometry_offset = source.geometry_offset;
            destination.material_offset = source.material_offset;
            destination.geometry_count = source.geometry_count;
        }
        for (std::size_t index = 0; index < frame_ray_tracing_material_data_.size(); ++index)
        {
            const ray_tracing::PathTracingMaterialRecord &source =
                frame_ray_tracing_material_data_[index];
            scene_data::PathTracingMaterialGpuData &destination = packed.material_data[index];
            destination.base_color = {source.base_color.x_, source.base_color.y_,
                                      source.base_color.z_, source.base_color.w_};
            destination.emissive = {source.emissive.x_, source.emissive.y_,
                                    source.emissive.z_, source.emissive.w_};
            destination.surface = {source.metallic, source.roughness, source.normal_scale, 0.0f};
            destination.surface[3] = static_cast<float>(
                source.metallic_channel + 4u * source.roughness_channel);
            destination.texture_indices[0] = source.base_color_texture_index;
            destination.texture_indices[1] = source.metallic_texture_index;
            destination.texture_indices[2] = source.roughness_texture_index;
        }
        for (std::size_t index = 0; index < frame_ray_tracing_light_data_.size(); ++index)
        {
            const ray_tracing::PathTracingLightRecord &source = frame_ray_tracing_light_data_[index];
            scene_data::PathTracingLightGpuData &destination = packed.light_data[index];
            destination.position_or_type = {source.position_or_type.x_, source.position_or_type.y_,
                                            source.position_or_type.z_, source.position_or_type.w_};
            destination.direction_and_range = {source.direction_and_range.x_,
                                                source.direction_and_range.y_,
                                                source.direction_and_range.z_,
                                                source.direction_and_range.w_};
            destination.color_intensity = {source.color_intensity.x_, source.color_intensity.y_,
                                           source.color_intensity.z_, source.color_intensity.w_};
            destination.parameters = {source.parameters.x_, source.parameters.y_,
                                      source.parameters.z_, source.parameters.w_};
        }

        update.records_packed = frame_ray_tracing_geometries_.size() +
                                frame_ray_tracing_instance_data_.size() +
                                frame_ray_tracing_material_data_.size() +
                                frame_ray_tracing_light_data_.size();
        update.cpu_pack_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pack_started).count();
        const auto *bytes = reinterpret_cast<const std::byte *>(&packed);
        graphics::RayTracingBufferReferenceTableDesc desc{};
        desc.data.assign(bytes, bytes + sizeof(packed));
        desc.address_patches = frame_ray_tracing_scene_address_patches_;
        const graphics::RayTracingBufferReferenceTableHandle next =
            owner.CreateRayTracingBufferReferenceTable(std::move(desc));
        if (!next.IsValid())
            return update;
        const auto previous = ray_tracing_scene_table_;
        ray_tracing_scene_table_ = next;
        MarkReferenceTableUpdated();
        if (previous.IsValid())
            owner.DestroyRayTracingBufferReferenceTable(previous);
        update.succeeded = true;
        update.table = ray_tracing_scene_table_;
        update.records_uploaded = update.records_packed;
        return update;
    }

    void RayTracingScene::MarkReferenceTableUpdated() noexcept
    {
        frame_ray_tracing_scene_table_dirty_ = false;
        ray_tracing_scene_table_lighting_signature_ = frame_ray_tracing_lighting_signature_;
    }

    void RayTracingScene::Cleanup(graphics::RenderBackend *backend)
    {
        graphics::RayTracingResourceOwner *const owner =
            backend != nullptr ? backend->GetRayTracingResourceOwner() : nullptr;
        CancelPreparedBuildResources(owner);
        if (owner != nullptr)
        {
            if (ray_tracing_scene_table_.IsValid())
            {
                owner->DestroyRayTracingBufferReferenceTable(ray_tracing_scene_table_);
            }
            for (const auto &[mesh, state] : ray_tracing_blas_)
            {
                (void)mesh;
                if (state.handle.IsValid())
                    owner->DestroyAccelerationStructure(state.handle);
            }
            if (ray_tracing_tlas_.IsValid())
                owner->DestroyAccelerationStructure(ray_tracing_tlas_);
        }

        ray_tracing_blas_.clear();
        ray_tracing_tlas_ = {};
        ray_tracing_tlas_capacity_ = 0;
        ray_tracing_tlas_built_ = false;
        ray_tracing_instance_signature_ = 0;
        frame_ray_tracing_instance_signature_ = 0;
        frame_ray_tracing_material_signature_ = 0;
        ray_tracing_scene_cache_world_revision_ = 0;
        ray_tracing_scene_cache_material_revision_ = 0;
        ray_tracing_scene_cache_instance_signature_ = 0;
        ray_tracing_scene_cache_material_signature_ = 0;
        ray_tracing_scene_cache_path_tracing_enabled_ = false;
        ray_tracing_scene_cache_valid_ = false;
        frame_ray_tracing_scene_table_dirty_ = true;
        ray_tracing_scene_table_lighting_signature_ = 0;
        frame_ray_tracing_lighting_signature_ = 0;
        ray_tracing_scene_record_cache_hits_total_ = 0;
        ray_tracing_scene_record_cache_misses_total_ = 0;
        ray_tracing_scene_table_ = {};
        frame_ray_tracing_geometries_.clear();
        frame_ray_tracing_instances_.clear();
        frame_ray_tracing_instance_data_.clear();
        frame_ray_tracing_material_data_.clear();
        frame_ray_tracing_light_data_.clear();
        frame_ray_tracing_scene_address_patches_.clear();
        frame_ray_tracing_mesh_builds_.clear();
        frame_ray_tracing_blas_builds_.clear();
        frame_ray_tracing_tlas_builds_.clear();
        frame_ray_tracing_blas_build_ = false;
        frame_ray_tracing_tlas_build_ = false;
        RebuildFrameViews();
    }
}
