#include "render_resource_resolver.h"

#include <cstddef>

#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "asset/asset_manager.h"
#include "data/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/sampler.h"
#include "graphics/backend/common/texture.h"
#include "resource/resource_pipeline.h"

namespace kpengine::render
{
    namespace
    {
        constexpr uint64_t kDefaultPipelineStateLayoutSignature = 1;

        bool IsSupportedMaterialPass(MaterialPass pass)
        {
            return pass == MaterialPass::Scene || pass == MaterialPass::GBuffer;
        }
    }

    RenderResourceResolver::RenderResourceResolver(graphics::RenderBackend &backend,
                                                     resource::ResourcePipeline &resource_pipeline)
        : backend_(&backend), resource_pipeline_(&resource_pipeline)
    {
    }

    graphics::PipelineHandle RenderResourceResolver::GetOrCreateDefaultPipeline(
        asset::AssetID program_id, asset::ShaderProgramResource &program,
        const MaterialPipelineState *material_state, bool bindless_texture_table_compatible,
        MaterialPass pass)
    {
        const uint64_t state_signature = material_state
                                             ? kDefaultPipelineStateLayoutSignature |
                                                   (static_cast<uint64_t>(material_state->blend_mode) << 8) |
                                                   (static_cast<uint64_t>(material_state->cull_mode) << 16) |
                                                   (static_cast<uint64_t>(material_state->double_sided) << 24)
                                             : kDefaultPipelineStateLayoutSignature;
        const uint64_t binding_model_signature = bindless_texture_table_compatible ? (1ull << 32) : 0;
        const uint64_t pass_signature = static_cast<uint64_t>(pass) << 40;
        const PipelineCacheKey key{program_id.Pack(), backend_->GetGraphicsContext().type,
                                   state_signature | binding_model_signature | pass_signature};
        const auto existing = pipeline_cache_.find(key);
        if (existing != pipeline_cache_.end())
        {
            return existing->second;
        }

        graphics::PipelineDesc desc{};
        if (!BuildDefaultPipelineDesc(program, desc, material_state,
                                      bindless_texture_table_compatible, pass))
        {
            return {};
        }

        const graphics::PipelineHandle handle = backend_->CreatePipelineResource(desc);
        if (handle.IsValid())
        {
            pipeline_cache_.emplace(key, handle);
        }
        return handle;
    }

    graphics::MeshHandle RenderResourceResolver::GetOrCreateMesh(
        asset::AssetID asset_id, const data::MeshData &data)
    {
        const uint64_t key = asset_id.Pack();
        const auto existing = mesh_cache_.find(key);
        if (existing != mesh_cache_.end())
        {
            return existing->second;
        }

        const graphics::MeshHandle handle = backend_->CreateMesh(data);
        if (handle.IsValid())
        {
            mesh_cache_.emplace(key, handle);
        }
        return handle;
    }

    TextureBinding RenderResourceResolver::GetOrCreateTextureBinding(
        asset::AssetID asset_id, const data::TextureData &data,
        MaterialTextureColorSpace color_space, const MaterialSamplerDesc *sampler_desc)
    {
        // The same asset can be sampled as sRGB (base color) and as linear
        // (normal/metallic/roughness/occlusion); each space needs its own GPU
        // texture because the format carries the color interpretation.
        const TextureCacheKey key{asset_id, color_space};
        const auto existing = texture_cache_.find(key);
        graphics::TextureHandle texture;
        if (existing != texture_cache_.end())
        {
            texture = existing->second;
        }
        else
        {
            graphics::TextureSettings settings = DefaultTextureSettings();
            settings.format = color_space == MaterialTextureColorSpace::Linear
                                  ? TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM
                                  : TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
            texture = backend_->CreateTexture(data, settings);
            if (texture.IsValid())
            {
                texture_cache_.emplace(key, texture);
            }
        }
        if (!texture.IsValid())
        {
            return {};
        }
        return {texture, sampler_desc ? GetOrCreateSampler(*sampler_desc) : GetOrCreateDefaultSampler()};
    }

    MaterialResolution RenderResourceResolver::ResolveTemplate(MaterialTemplateHandle handle,
                                                                const MaterialTemplateDesc &desc)
    {
        auto program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(
            desc.shader_program);
        if (!program)
        {
            return {MaterialResourceState::Pending, "shader program asset is not loaded"};
        }

        const bool use_bindless_pipeline = desc.bindless_texture_table_compatible &&
                                           backend_->GetCapabilities().SupportsBindlessTextures();
        const asset::ShaderProgramVariant variant = use_bindless_pipeline
                                                        ? asset::ShaderProgramVariant::Bindless
                                                        : asset::ShaderProgramVariant::Bound;
        const auto shaders = program->GatherShaders(variant);
        resource_pipeline_->ProcessShader(shaders);
        for (const asset::ShaderPtr &shader : shaders)
        {
            if (!shader || shader->status == asset::ShaderStatus::CompileFailed)
            {
                return {MaterialResourceState::Failed, "shader program compilation failed"};
            }
        }
        // A pipeline per compatible pass with a registered builder. Passes that
        // have no builder yet (ShadowDepth until D4) are skipped, not failed —
        // the template stays resolvable for the passes it can actually run in.
        std::unordered_map<MaterialPass, graphics::PipelineHandle> pipelines;
        for (MaterialPass pass : desc.compatible_passes)
        {
            if (!IsSupportedMaterialPass(pass))
            {
                continue;
            }
            const graphics::PipelineHandle pipeline = GetOrCreateDefaultPipeline(
                desc.shader_program, *program, &desc.pipeline_state, use_bindless_pipeline, pass);
            if (!pipeline.IsValid())
            {
                return {MaterialResourceState::Failed, "pipeline creation failed"};
            }
            pipelines.emplace(pass, pipeline);
        }
        if (pipelines.empty())
        {
            return {MaterialResourceState::Failed, "no compatible pass pipeline could be created"};
        }
        material_pipelines_[handle] = std::move(pipelines);
        return {MaterialResourceState::Ready, {}};
    }

    MaterialResolution RenderResourceResolver::ResolveInstance(MaterialInstanceHandle handle,
        const MaterialTemplateDesc &desc,
        const std::vector<MaterialParameterValue> &effective_values)
    {
        ReleaseInstance(handle);
        material_texture_bindings_.erase(handle);
        auto &asset_manager = asset::AssetManager::GetInstance();
        ResolvedMaterialTextureBindings bindings;
        for (uint32_t parameter_id = 0; parameter_id < effective_values.size(); ++parameter_id)
        {
            const MaterialParameterValue &value = effective_values[parameter_id];
            const auto *texture_value = std::get_if<MaterialTextureSamplerValue>(&value);
            if (!texture_value)
            {
                continue;
            }
            auto texture = asset_manager.GetResource<asset::TextureResource>(texture_value->texture_asset);
            if (!texture || !texture->data)
            {
                return {MaterialResourceState::Pending, "material texture asset is not loaded"};
            }
            const TextureBinding binding =
                GetOrCreateTextureBinding(texture_value->texture_asset, *texture->data,
                                          texture_value->color_space, &texture_value->sampler);
            if (!binding.texture.IsValid() || !binding.sampler.IsValid())
            {
                return {MaterialResourceState::Failed, "material texture binding creation failed"};
            }
            bindings.textures.emplace(parameter_id, binding);
        }
        if (desc.bindless_texture_table_compatible &&
            backend_->GetCapabilities().SupportsBindlessTextures())
        {
            bool acquired_all_slots = true;
            for (const auto &[parameter_id, binding] : bindings.textures)
            {
                const graphics::BindlessTextureHandle slot =
                    backend_->AcquireBindlessTexture(binding.texture, binding.sampler);
                if (!slot.IsValid())
                {
                    acquired_all_slots = false;
                    break;
                }
                bindings.bindless_slots.emplace(parameter_id, slot);
            }
            if (acquired_all_slots)
            {
                bindings.uses_bindless_textures = true;
            }
            else
            {
                for (const auto &[parameter_id, slot] : bindings.bindless_slots)
                {
                    (void)parameter_id;
                    backend_->ReleaseBindlessTexture(slot);
                }
                bindings.bindless_slots.clear();
            }
        }
        material_texture_bindings_[handle] = std::move(bindings);
        return {MaterialResourceState::Ready, {}};
    }

    void RenderResourceResolver::ReleaseTemplate(MaterialTemplateHandle handle)
    {
        material_pipelines_.erase(handle);
    }

    void RenderResourceResolver::ReleaseInstance(MaterialInstanceHandle handle)
    {
        const auto found = material_texture_bindings_.find(handle);
        if (found == material_texture_bindings_.end())
        {
            return;
        }
        if (backend_)
        {
            for (const auto &[parameter_id, slot] : found->second.bindless_slots)
            {
                (void)parameter_id;
                backend_->ReleaseBindlessTexture(slot);
            }
        }
        material_texture_bindings_.erase(found);
    }

    graphics::PipelineHandle RenderResourceResolver::FindMaterialPipeline(
        MaterialTemplateHandle handle, MaterialPass pass) const
    {
        const auto it = material_pipelines_.find(handle);
        if (it == material_pipelines_.end())
        {
            return {};
        }
        const auto pass_it = it->second.find(pass);
        return pass_it != it->second.end() ? pass_it->second : graphics::PipelineHandle{};
    }

    const RenderResourceResolver::ResolvedMaterialTextureBindings *
    RenderResourceResolver::FindTextureBindings(MaterialInstanceHandle handle) const
    {
        const auto it = material_texture_bindings_.find(handle);
        return it != material_texture_bindings_.end() ? &it->second : nullptr;
    }

    bool RenderResourceResolver::UsesBindlessTextures(MaterialInstanceHandle handle) const
    {
        const auto *bindings = FindTextureBindings(handle);
        return bindings && bindings->uses_bindless_textures;
    }

    void RenderResourceResolver::Cleanup()
    {
        if (!backend_)
        {
            return;
        }

        std::vector<MaterialInstanceHandle> instances;
        instances.reserve(material_texture_bindings_.size());
        for (const auto &[instance, bindings] : material_texture_bindings_)
        {
            (void)bindings;
            instances.push_back(instance);
        }
        for (MaterialInstanceHandle instance : instances)
        {
            ReleaseInstance(instance);
        }
        for (const auto &[key, handle] : mesh_cache_)
        {
            (void)key;
            backend_->DestroyMesh(handle);
        }
        mesh_cache_.clear();
        for (const auto &[key, handle] : texture_cache_)
        {
            (void)key;
            backend_->DestroyTexture(handle);
        }
        texture_cache_.clear();
        if (default_sampler_handle_.IsValid())
        {
            backend_->DestroySampler(default_sampler_handle_);
            default_sampler_handle_ = {};
        }
        for (const auto &[key, handle] : pipeline_cache_)
        {
            (void)key;
            backend_->DestroyPipelineResource(handle);
        }
        pipeline_cache_.clear();
        for (const auto &[key, handle] : material_sampler_cache_)
        {
            (void)key;
            backend_->DestroySampler(handle);
        }
        material_sampler_cache_.clear();
        backend_ = nullptr;
        resource_pipeline_ = nullptr;
        material_pipelines_.clear();
        material_texture_bindings_.clear();
    }

    bool RenderResourceResolver::BuildDefaultPipelineDesc(asset::ShaderProgramResource &program,
                                                           graphics::PipelineDesc &out_desc,
                                                           const MaterialPipelineState *material_state,
                                                           bool bindless_texture_table_compatible,
                                                           MaterialPass pass)
    {
        const asset::ShaderProgramVariant variant = bindless_texture_table_compatible
                                                        ? asset::ShaderProgramVariant::Bindless
                                                        : asset::ShaderProgramVariant::Bound;
        const auto vert_shader = program.GetShader(ShaderStage::SHADER_STAGE_VERTEX,
                                                   ShaderFormat::SHADER_FORMAT_GLSL, variant);
        const auto frag_shader = program.GetShader(ShaderStage::SHADER_STAGE_FRAGMENT,
                                                   ShaderFormat::SHADER_FORMAT_GLSL, variant);
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status != asset::ShaderStatus::Ready ||
            frag_shader->status != asset::ShaderStatus::Ready)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        if (pass == MaterialPass::GBuffer)
        {
            // Deferred G-buffer: canonical 5-attribute layout (matches the
            // data::Vertex field order and the audited tangent convention) into
            // a 3-color MRT + depth. Binding 4 is left open for the D5 frame
            // lighting block; sampler slots 2/5/6/7/8 mirror the StandardPbr
            // material parameter ABI in material_asset_resolver.cpp.
            desc.color_attachment_formats = {
                TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                TextureFormat::TEXTURE_FORMAT_RGBA16F,
                TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM};
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
            desc.binding_descs = {{0, sizeof(data::Vertex), false}};
            desc.attri_descs = {
                {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                 offsetof(data::Vertex, position)},
                {1, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                 offsetof(data::Vertex, normal)},
                {2, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 offsetof(data::Vertex, tex_coord)},
                {3, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                 offsetof(data::Vertex, tangent)},
                {4, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                 offsetof(data::Vertex, bitangent)},
            };
            desc.descriptor_binding_descs = {
                {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_VERTEX},
                 {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_VERTEX},
                 {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                  ShaderStage::SHADER_STAGE_FRAGMENT},
                 {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_FRAGMENT},
                 {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                  ShaderStage::SHADER_STAGE_FRAGMENT},
                 {6, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                  ShaderStage::SHADER_STAGE_FRAGMENT},
                 {7, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                  ShaderStage::SHADER_STAGE_FRAGMENT},
                 {8, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                  ShaderStage::SHADER_STAGE_FRAGMENT}},
            };
        }
        else
        {
            desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
            desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
            desc.binding_descs = {{0, sizeof(data::Vertex), false}};
            desc.attri_descs = {
                {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
                 offsetof(data::Vertex, position)},
                {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
                 offsetof(data::Vertex, tex_coord)},
            };
            desc.descriptor_binding_descs = {
                {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_VERTEX},
                 {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_VERTEX},
                 {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
                  ShaderStage::SHADER_STAGE_FRAGMENT}},
            };
            if (!bindless_texture_table_compatible)
            {
                desc.descriptor_binding_descs[0].insert(
                    desc.descriptor_binding_descs[0].begin() + 2,
                    {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                     ShaderStage::SHADER_STAGE_FRAGMENT});
            }
        }
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        if (material_state)
        {
            desc.raster_state.cull_mode = material_state->double_sided
                                               ? graphics::CullMode::CULL_MODE_NONE
                                               : material_state->cull_mode == MaterialCullMode::Back
                                                     ? graphics::CullMode::CULL_MODE_BACK
                                                     : graphics::CullMode::CULL_MODE_FRONT;
            desc.blend_attachment_state.blend_enabled =
                material_state->blend_mode == MaterialBlendMode::AlphaBlend;
            if (desc.blend_attachment_state.blend_enabled)
            {
                desc.blend_attachment_state.src_color_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_SRC_ALPHA;
                desc.blend_attachment_state.dst_color_blend_factor =
                    graphics::BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            }
        }
        out_desc = std::move(desc);
        return true;
    }

    graphics::TextureSettings RenderResourceResolver::DefaultTextureSettings()
    {
        graphics::TextureSettings settings{};
        settings.mip_levels = 1;
        settings.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        settings.usage = graphics::TextureUsage::TEXTURE_USAGE_TRANSFER_DST |
                         graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;
        return settings;
    }

    graphics::SamplerHandle RenderResourceResolver::GetOrCreateDefaultSampler()
    {
        if (default_sampler_handle_.IsValid())
        {
            return default_sampler_handle_;
        }

        graphics::SamplerSettings settings{};
        settings.max_anisotropy = 16.0f;
        default_sampler_handle_ = backend_->CreateSampler(settings);
        return default_sampler_handle_;
    }

    graphics::SamplerHandle RenderResourceResolver::GetOrCreateSampler(
        const MaterialSamplerDesc &desc)
    {
        const uint64_t key = static_cast<uint64_t>(desc.min_filter) |
                             (static_cast<uint64_t>(desc.mag_filter) << 8) |
                             (static_cast<uint64_t>(desc.address_u) << 16) |
                             (static_cast<uint64_t>(desc.address_v) << 24) |
                             (static_cast<uint64_t>(desc.address_w) << 32);
        const auto existing = material_sampler_cache_.find(key);
        if (existing != material_sampler_cache_.end())
        {
            return existing->second;
        }
        graphics::SamplerSettings settings{};
        settings.min_filter = desc.min_filter == MaterialSamplerFilter::Linear
                                  ? graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR
                                  : graphics::SamplerFilterType::SAMPLER_FILTER_NEAREST;
        settings.mag_filter = desc.mag_filter == MaterialSamplerFilter::Linear
                                  ? graphics::SamplerFilterType::SAMPLER_FILTER_LINEAR
                                  : graphics::SamplerFilterType::SAMPLER_FILTER_NEAREST;
        settings.address_mode_u = desc.address_u == MaterialSamplerAddressMode::Repeat
                                      ? graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT
                                      : graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.address_mode_v = desc.address_v == MaterialSamplerAddressMode::Repeat
                                      ? graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT
                                      : graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.address_mode_w = desc.address_w == MaterialSamplerAddressMode::Repeat
                                      ? graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT
                                      : graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        const graphics::SamplerHandle handle = backend_->CreateSampler(settings);
        if (handle.IsValid())
        {
            material_sampler_cache_.emplace(key, handle);
        }
        return handle;
    }
}
