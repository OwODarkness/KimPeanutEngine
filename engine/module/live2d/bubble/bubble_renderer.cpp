#include "bubble_renderer.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/shader_program.h"
#include "config/path.h"
#include "data/texture.h"
#include "dot_matrix.h"
#include "graphics/backend/common/buffer_types.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/sampler.h"
#include "graphics/backend/common/texture.h"
#include "resource/resource_pipeline.h"

namespace kpengine::live2d
{
    namespace
    {
        constexpr std::uint32_t kQuadVertexCount = 4u;
        constexpr std::uint32_t kQuadIndexCount = 6u;
        constexpr std::uint32_t kQuadPositionBinding = 0u;

        // This module's own copy of the two shader-loading helpers Live2D and the
        // panel each carry. Three copies is the point at which they should be
        // promoted; that is deliberately not done inside a stage that is adding a
        // renderer, and is recorded in the module's roadmap instead.
        asset::ShaderProgramResource *LoadProgram(resource::ResourcePipeline &pipeline,
                                                  const char *file,
                                                  std::string &diagnostic)
        {
            asset::AssetManager &manager = asset::AssetManager::GetInstance();
            const asset::AssetID id = manager.LoadSync(GetShaderDirectory() + file);
            const auto program = manager.GetResource<asset::ShaderProgramResource>(id);
            if (!id.IsValid() || !program)
            {
                diagnostic = std::string("bubble shader program failed to load: ") + file;
                return nullptr;
            }
            pipeline.ProcessShader(program->GatherShaders(asset::ShaderProgramVariant::Bound));
            return program.get();
        }

        asset::ShaderData *GetStage(asset::ShaderProgramResource *program, ShaderStage stage)
        {
            if (program == nullptr)
            {
                return nullptr;
            }
            const auto shader = program->GetShader(stage, ShaderFormat::SHADER_FORMAT_GLSL,
                                                   asset::ShaderProgramVariant::Bound);
            return shader && shader->data ? shader->data.get() : nullptr;
        }

        std::array<float, 8> QuadPositions()
        {
            return {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
        }

        std::array<std::uint16_t, kQuadIndexCount> QuadIndices()
        {
            return {0u, 1u, 2u, 0u, 2u, 3u};
        }

        std::vector<std::byte> ToBytes(const BubbleDrawConstants &constants)
        {
            const auto *begin = reinterpret_cast<const std::byte *>(&constants);
            return std::vector<std::byte>(begin, begin + sizeof(constants));
        }
    }

    BubbleRenderer::~BubbleRenderer()
    {
        Cleanup();
    }

    bool BubbleRenderer::Initialize(graphics::RenderBackend &backend,
                                    const TextureFormat color_format,
                                    std::string &diagnostic)
    {
        if (initialized_)
        {
            return true;
        }
        backend_ = &backend;

        if (!CreatePipeline(color_format, diagnostic) || !CreateQuadGeometry(diagnostic))
        {
            Cleanup();
            return false;
        }

        initialized_ = true;
        return true;
    }

    bool BubbleRenderer::CreatePipeline(const TextureFormat color_format,
                                        std::string &diagnostic)
    {
        resource::ResourcePipeline pipeline;
        pipeline.Initialize({backend_->GetGraphicsAPI()});
        asset::ShaderProgramResource *bubble =
            LoadProgram(pipeline, "bubble.shader", diagnostic);
        if (bubble == nullptr)
        {
            return false;
        }

        const std::array<graphics::VertexBindingDesc, 1> bindings{{
            {kQuadPositionBinding, sizeof(float) * 2u, false}}};
        const std::array<graphics::VertexAttributionDesc, 1> attributes{{
            {0u, kQuadPositionBinding, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             0u}}};
        // The placement is read by the vertex stage and everything else by the
        // fragment stage, so the block is declared in both and the descriptor
        // covers both.
        const std::array<graphics::DescriptorBindingDesc, 2> descriptors{{
            {kBubbleConstantsBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             static_cast<ShaderStage>(static_cast<std::uint32_t>(ShaderStage::SHADER_STAGE_VERTEX) |
                                      static_cast<std::uint32_t>(ShaderStage::SHADER_STAGE_FRAGMENT))},
            {kBubbleTextBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT}}};

        graphics::PipelineDesc desc{};
        desc.vert_shader = GetStage(bubble, ShaderStage::SHADER_STAGE_VERTEX);
        desc.frag_shader = GetStage(bubble, ShaderStage::SHADER_STAGE_FRAGMENT);
        desc.binding_descs.assign(bindings.begin(), bindings.end());
        desc.attri_descs.assign(attributes.begin(), attributes.end());
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.color_attachment_formats = {color_format};
        // The bubble has to composite over the model rather than punch an opaque
        // rectangle through it, which is what the fragment shader's coverage is
        // for. This is the one place the bubble differs from the panel, which
        // draws opaquely into a target of its own.
        desc.blend_attachment_state.blend_enabled = true;
        desc.blend_attachment_state.src_color_blend_factor =
            graphics::BlendFactor::BLEND_FACTOR_SRC_ALPHA;
        desc.blend_attachment_state.dst_color_blend_factor =
            graphics::BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        desc.blend_attachment_state.src_alpha_blend_factor =
            graphics::BlendFactor::BLEND_FACTOR_ONE;
        desc.blend_attachment_state.dst_alpha_blend_factor =
            graphics::BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        desc.descriptor_binding_descs = {
            std::vector<graphics::DescriptorBindingDesc>(descriptors.begin(),
                                                         descriptors.end())};

        pipeline_ = backend_->CreatePipelineResource(desc);
        if (!pipeline_.IsValid())
        {
            diagnostic = "bubble pipeline creation failed";
            return false;
        }

        graphics::SamplerSettings sampler_settings{};
        // NEAREST, because one texel is one dot and the text is meant to read as
        // a dot matrix. A linear filter would blur exactly the thing the panel
        // exists to show.
        sampler_settings.mag_filter = graphics::SamplerFilterType::SAMPLER_FILTER_NEAREST;
        sampler_settings.min_filter = graphics::SamplerFilterType::SAMPLER_FILTER_NEAREST;
        sampler_settings.mipmap_mode = graphics::SamplerMipmapMode::SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_settings.address_mode_u =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.address_mode_v =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.address_mode_w =
            graphics::SamplerAddressMode::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_settings.enable_anisotropy = false;
        sampler_settings.max_lod = 0.0f;
        sampler_ = backend_->CreateSampler(sampler_settings);
        if (!sampler_.IsValid())
        {
            diagnostic = "bubble sampler creation failed";
            return false;
        }

        return diagnostic.empty();
    }

    bool BubbleRenderer::CreateQuadGeometry(std::string &diagnostic)
    {
        const std::array<float, 8> positions = QuadPositions();
        const std::array<std::uint16_t, kQuadIndexCount> indices = QuadIndices();

        quad_vertices_ = backend_->CreateBuffer(
            {graphics::BufferRole::Vertex, graphics::BufferUpdateMode::Immutable,
             positions.size() * sizeof(float)},
            std::as_bytes(std::span{positions}));
        quad_indices_ = backend_->CreateBuffer(
            {graphics::BufferRole::Index, graphics::BufferUpdateMode::Immutable,
             indices.size() * sizeof(std::uint16_t)},
            std::as_bytes(std::span{indices}));
        if (!quad_vertices_.IsValid() || !quad_indices_.IsValid())
        {
            diagnostic = "bubble quad geometry creation failed";
            return false;
        }
        return true;
    }

    bool BubbleRenderer::UploadText(const panel::DotMatrix &ink, std::string &diagnostic)
    {
        if (backend_ == nullptr || !initialized_)
        {
            diagnostic = "bubble renderer is not initialized";
            return false;
        }
        if (ink.Width() == 0u || ink.Height() == 0u)
        {
            diagnostic = "bubble text has no extent";
            return false;
        }

        // One byte per dot, which is the shader's sampler rather than its bit
        // index. The panel packs one bit per dot, so this is where the two
        // representations meet.
        data::TextureData mask{};
        mask.width = ink.Width();
        mask.height = ink.Height();
        mask.depth = 1u;
        mask.array_layers = 1u;
        mask.format = TextureFormat::TEXTURE_FORMAT_R8_UNORM;
        mask.pixels.resize(static_cast<std::size_t>(ink.Width()) * ink.Height());
        for (std::uint32_t row = 0u; row < ink.Height(); ++row)
        {
            for (std::uint32_t column = 0u; column < ink.Width(); ++column)
            {
                mask.pixels[(static_cast<std::size_t>(row) * ink.Width()) + column] =
                    ink.TestDot(column, row) ? std::uint8_t{0xFFu} : std::uint8_t{0u};
            }
        }

        // Measured here rather than asked of the caller: the mask is what is
        // being uploaded, so this is where its extent is known, and one scan
        // answers for both the upload and the shader's lookup.
        const panel::DotBounds bounds = panel::LitBounds(ink);
        const std::uint32_t left = bounds.empty ? 0u : bounds.left;
        const std::uint32_t top = bounds.empty ? 0u : bounds.top;
        const std::uint32_t right = bounds.empty ? ink.Width() - 1u : bounds.right;
        const std::uint32_t bottom = bounds.empty ? ink.Height() - 1u : bounds.bottom;
        text_mask_info_.uv = MakeInkUv(left, top, right, bottom, ink.Width(),
                                       ink.Height());
        // The dot count, not the texel size: it is what makes the bubble's grid
        // pitch match the text's, so a character lands inside a cell rather than
        // straddling two.
        text_mask_info_.dots_x = right - left + 1u;
        text_mask_info_.dots_y = bottom - top + 1u;

        graphics::TextureSettings settings{};
        settings.format = TextureFormat::TEXTURE_FORMAT_R8_UNORM;
        settings.mip_levels = 1u;
        settings.usage = graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;

        const graphics::TextureHandle replacement = backend_->CreateTexture(mask, settings);
        if (!replacement.IsValid())
        {
            diagnostic = "bubble text upload failed";
            return false;
        }

        backend_->WaitIdle();
        if (text_mask_.IsValid())
        {
            backend_->DestroyTexture(text_mask_);
        }
        text_mask_ = replacement;
        return true;
    }

    bool BubbleRenderer::BuildDraws(const BubbleLayout &layout,
                                    const BubblePlacement &placement,
                                    const BubbleAppearance &appearance,
                                    const graphics::Viewport &viewport,
                                    std::vector<render::SubmissionDraw> &out,
                                    std::string &diagnostic) const
    {
        if (!initialized_ || backend_ == nullptr)
        {
            diagnostic = "bubble renderer is not initialized";
            return false;
        }
        if (!text_mask_.IsValid())
        {
            diagnostic = "bubble renderer has no text; upload text before drawing";
            return false;
        }
        // A progress of zero is a bubble that has not appeared yet. Refusing to
        // draw it is more honest than drawing a zero-sized quad, which would
        // validate and produce nothing.
        if (placement.progress <= 0.0f)
        {
            return true;
        }

        const BubbleDrawConstants constants =
            MakeBubbleConstants(layout, placement, appearance, text_mask_info_);

        render::SubmissionDraw draw{};
        draw.pipeline = pipeline_;
        draw.geometry.vertices = {{0u, quad_vertices_, 0u}};
        draw.geometry.indices = {quad_indices_, 0u, graphics::IndexElementType::UInt16};

        render::SubmissionUniformData uniform{};
        uniform.set = 0u;
        uniform.binding = kBubbleConstantsBinding;
        uniform.bytes = ToBytes(constants);
        draw.uniforms.push_back(std::move(uniform));

        render::SubmissionSampledTexture texture{};
        texture.set = 0u;
        texture.binding = kBubbleTextBinding;
        texture.texture = text_mask_;
        texture.sampler = sampler_;
        draw.textures.push_back(texture);

        draw.viewport = viewport;
        draw.index_count = kQuadIndexCount;
        draw.first_index = 0u;
        draw.vertex_offset = 0;

        out.push_back(std::move(draw));
        return true;
    }

    std::uint32_t BubbleRenderer::GetLiveGpuHandleCount() const noexcept
    {
        std::uint32_t count = 0u;
        count += pipeline_.IsValid() ? 1u : 0u;
        count += sampler_.IsValid() ? 1u : 0u;
        count += quad_vertices_.IsValid() ? 1u : 0u;
        count += quad_indices_.IsValid() ? 1u : 0u;
        count += text_mask_.IsValid() ? 1u : 0u;
        return count;
    }

    void BubbleRenderer::DestroyText() noexcept
    {
        if (backend_ != nullptr && text_mask_.IsValid())
        {
            backend_->DestroyTexture(text_mask_);
        }
        text_mask_ = {};
    }

    void BubbleRenderer::DestroyQuadGeometry() noexcept
    {
        if (backend_ != nullptr)
        {
            if (quad_vertices_.IsValid())
            {
                backend_->DestroyBufferResource(quad_vertices_);
            }
            if (quad_indices_.IsValid())
            {
                backend_->DestroyBufferResource(quad_indices_);
            }
        }
        quad_vertices_ = {};
        quad_indices_ = {};
    }

    void BubbleRenderer::DestroyPipeline() noexcept
    {
        if (backend_ != nullptr)
        {
            if (pipeline_.IsValid())
            {
                backend_->DestroyPipelineResource(pipeline_);
            }
            if (sampler_.IsValid())
            {
                backend_->DestroySampler(sampler_);
            }
        }
        pipeline_ = {};
        sampler_ = {};
    }

    void BubbleRenderer::Cleanup() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->WaitIdle();
        }
        // Reverse creation order: the text the draw samples first, then the
        // geometry, then the sampler and pipeline that consume them.
        DestroyText();
        DestroyQuadGeometry();
        DestroyPipeline();
        initialized_ = false;
        backend_ = nullptr;
    }
}
