#include "panel_renderer.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "asset/asset_manager.h"
#include "asset/shader_program.h"
#include "config/path.h"
#include "data/texture.h"
#include "dot_matrix.h"
#include "graphics/backend/common/buffer_types.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/sampler.h"
#include "graphics/backend/common/texture.h"
#include "render/frame_context.h"
#include "render/render_submission_executor.h"
#include "resource/resource_pipeline.h"

namespace kpengine::panel
{
    namespace
    {
        constexpr std::uint32_t kQuadPositionBinding = 0u;

        // This duplicates the Live2D renderer's two loading helpers rather than
        // sharing them. Two call sites is not yet a consumer that justifies a
        // common owner; a third module should promote them, not a second.
        asset::ShaderProgramResource *LoadProgram(resource::ResourcePipeline &pipeline,
                                                  const char *file,
                                                  std::string &diagnostic)
        {
            asset::AssetManager &manager = asset::AssetManager::GetInstance();
            const asset::AssetID id = manager.LoadSync(GetShaderDirectory() + file);
            const auto program = manager.GetResource<asset::ShaderProgramResource>(id);
            if (!id.IsValid() || !program)
            {
                diagnostic = std::string("panel shader program failed to load: ") + file;
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

        // The quad is authored directly in clip space, so it is the same four
        // positions on every backend and needs no per-frame upload.
        std::array<float, 8> QuadPositions()
        {
            return {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
        }

        std::array<std::uint16_t, kPanelQuadIndexCount> QuadIndices()
        {
            return {0u, 1u, 2u, 0u, 2u, 3u};
        }
    }

    PanelRenderer::~PanelRenderer()
    {
        Cleanup();
    }

    bool PanelRenderer::Initialize(graphics::RenderBackend &backend, std::uint32_t width,
                                   std::uint32_t height, std::string &diagnostic)
    {
        if (initialized_)
        {
            return true;
        }
        backend_ = &backend;

        if (!CreatePipeline(diagnostic) || !CreateQuadGeometry(diagnostic) ||
            !CreateOutputTarget(width, height, diagnostic))
        {
            Cleanup();
            return false;
        }

        initialized_ = true;
        return true;
    }

    bool PanelRenderer::CreatePipeline(std::string &diagnostic)
    {
        resource::ResourcePipeline pipeline;
        pipeline.Initialize({backend_->GetGraphicsAPI()});
        asset::ShaderProgramResource *dots = LoadProgram(pipeline, "panel_dots.shader", diagnostic);
        if (dots == nullptr)
        {
            return false;
        }

        const std::array<graphics::VertexBindingDesc, 1> bindings{{
            {kQuadPositionBinding, sizeof(float) * 2u, false}}};
        const std::array<graphics::VertexAttributionDesc, 1> attributes{{
            {0u, kQuadPositionBinding, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             0u}}};
        // Both bindings are fragment-only: the vertex shader derives its texture
        // coordinate from clip space and so declares no uniform block. Keeping
        // the layout exactly what the shaders use avoids reflection disagreeing
        // with a stage that declares a block it never reads.
        const std::array<graphics::DescriptorBindingDesc, 2> descriptors{{
            {kPanelConstantsBinding, 1u, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
             ShaderStage::SHADER_STAGE_FRAGMENT},
            {kPanelDotMaskBinding, 1u,
             graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
             ShaderStage::SHADER_STAGE_FRAGMENT}}};

        graphics::PipelineDesc desc{};
        desc.vert_shader = GetStage(dots, ShaderStage::SHADER_STAGE_VERTEX);
        desc.frag_shader = GetStage(dots, ShaderStage::SHADER_STAGE_FRAGMENT);
        desc.binding_descs.assign(bindings.begin(), bindings.end());
        desc.attri_descs.assign(attributes.begin(), attributes.end());
        // The quad covers the whole target, so culling would only risk losing it
        // to a winding convention.
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.color_attachment_formats = {output_color_format_};
        desc.blend_attachment_state.blend_enabled = false;
        desc.descriptor_binding_descs = {
            std::vector<graphics::DescriptorBindingDesc>(descriptors.begin(),
                                                         descriptors.end())};

        pipeline_ = backend_->CreatePipelineResource(desc);
        if (!pipeline_.IsValid())
        {
            diagnostic = "panel pipeline creation failed";
            return false;
        }

        graphics::SamplerSettings sampler_settings{};
        // This is what makes the dots blocks. One texel is one dot, and the quad
        // is drawn larger than the mask, so NEAREST is the whole expansion.
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
            diagnostic = "panel sampler creation failed";
            return false;
        }

        return diagnostic.empty();
    }

    bool PanelRenderer::CreateQuadGeometry(std::string &diagnostic)
    {
        const std::array<float, 8> positions = QuadPositions();
        const std::array<std::uint16_t, kPanelQuadIndexCount> indices = QuadIndices();

        // Immutable: the quad never changes, and an immutable buffer cannot be
        // written through the submission's buffer_writes.
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
            diagnostic = "panel quad geometry creation failed";
            return false;
        }
        return true;
    }

    bool PanelRenderer::UploadPanel(const DotMatrix &matrix, std::string &diagnostic)
    {
        if (backend_ == nullptr || !initialized_)
        {
            diagnostic = "panel renderer is not initialized";
            return false;
        }
        if (matrix.Width() == 0u || matrix.Height() == 0u)
        {
            diagnostic = "panel dot matrix has no extent";
            return false;
        }

        // One byte per dot. The matrix is packed one bit per dot, so this is
        // where the two representations are reconciled: the shader wants a
        // sampler, not a bit index.
        data::TextureData mask{};
        mask.width = matrix.Width();
        mask.height = matrix.Height();
        mask.depth = 1u;
        mask.array_layers = 1u;
        mask.format = TextureFormat::TEXTURE_FORMAT_R8_UNORM;
        mask.pixels.resize(static_cast<std::size_t>(matrix.Width()) * matrix.Height());
        for (std::uint32_t row = 0u; row < matrix.Height(); ++row)
        {
            for (std::uint32_t column = 0u; column < matrix.Width(); ++column)
            {
                mask.pixels[(static_cast<std::size_t>(row) * matrix.Width()) + column] =
                    matrix.TestDot(column, row) ? std::uint8_t{0xFFu} : std::uint8_t{0u};
            }
        }

        graphics::TextureSettings settings{};
        settings.format = TextureFormat::TEXTURE_FORMAT_R8_UNORM;
        settings.mip_levels = 1u;
        settings.usage = graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;

        const graphics::TextureHandle replacement = backend_->CreateTexture(mask, settings);
        if (!replacement.IsValid())
        {
            diagnostic = "panel dot mask upload failed";
            return false;
        }

        // The backend has no in-place upload, so a content change recreates the
        // texture. The old one may still be referenced by work already
        // submitted, so it is released only after the device is idle.
        backend_->WaitIdle();
        if (dot_mask_.IsValid())
        {
            backend_->DestroyTexture(dot_mask_);
        }
        dot_mask_ = replacement;
        return true;
    }

    PanelRenderProxy PanelRenderer::MakeProxy() const noexcept
    {
        PanelRenderProxy proxy;
        proxy.pipeline = pipeline_;
        proxy.dot_mask = dot_mask_;
        proxy.sampler = sampler_;
        proxy.quad_vertices = quad_vertices_;
        proxy.quad_indices = quad_indices_;
        proxy.output_target = output_target_;
        proxy.output_width = output_width_;
        proxy.output_height = output_height_;
        proxy.index_count = kPanelQuadIndexCount;
        return proxy;
    }

    bool PanelRenderer::Record(render::FrameContext &frame_context,
                               graphics::CommandRecorder &recorder,
                               const PanelRenderPlanOptions &options,
                               std::string &diagnostic)
    {
        if (!initialized_ || backend_ == nullptr)
        {
            diagnostic = "panel renderer is not initialized";
            return false;
        }
        if (!dot_mask_.IsValid())
        {
            diagnostic = "panel renderer has no dot mask; upload a panel before recording";
            return false;
        }

        const PanelRenderPlanResult plan = PanelRenderPlanner::Plan(MakeProxy(), options);
        if (!plan.succeeded)
        {
            diagnostic = plan.diagnostic;
            return false;
        }

        const render::RenderSubmissionExecutionResult execution =
            render::RenderSubmissionExecutor::Execute(plan.work, frame_context, recorder);
        if (!execution.succeeded)
        {
            // A partial result must never be presented; the caller decides.
            diagnostic = execution.diagnostic;
            return false;
        }
        return true;
    }

    bool PanelRenderer::CreateOutputTarget(std::uint32_t width, std::uint32_t height,
                                           std::string &diagnostic)
    {
        if (width == 0u || height == 0u)
        {
            diagnostic = "panel output extent must be non-zero";
            return false;
        }
        if (output_target_.IsValid() && output_width_ == width && output_height_ == height)
        {
            return true;
        }

        // Build the replacement first, so a failed create leaves the previous
        // target and view untouched and the caller keeps its last good output.
        graphics::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        graphics::RenderTargetColorAttachment attachment{};
        attachment.format = output_color_format_;
        attachment.load_op = graphics::RenderTargetLoadOp::Clear;
        attachment.store_op = graphics::RenderTargetStoreOp::Store;
        attachment.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        desc.color_attachments = {{attachment}};

        const graphics::RenderTargetHandle replacement = backend_->CreateRenderTarget(desc);
        if (!replacement.IsValid())
        {
            diagnostic = "panel renderer could not create the replacement render target";
            return false;
        }
        const graphics::RenderTargetView replacement_view =
            backend_->GetRenderTargetView(replacement);
        if (!replacement_view.IsValid())
        {
            backend_->DestroyRenderTarget(replacement);
            diagnostic = "panel renderer could not resolve the replacement render target view";
            return false;
        }

        backend_->WaitIdle();
        if (output_target_.IsValid())
        {
            backend_->DestroyRenderTarget(output_target_);
        }
        output_target_ = replacement;
        output_view_ = replacement_view;
        output_width_ = width;
        output_height_ = height;
        return true;
    }

    bool PanelRenderer::ResizeOutput(std::uint32_t width, std::uint32_t height,
                                     std::string &diagnostic)
    {
        if (backend_ == nullptr || !initialized_)
        {
            diagnostic = "panel renderer is not initialized";
            return false;
        }
        return CreateOutputTarget(width, height, diagnostic);
    }

    std::uint32_t PanelRenderer::GetLiveGpuHandleCount() const noexcept
    {
        std::uint32_t count = 0u;
        count += pipeline_.IsValid() ? 1u : 0u;
        count += sampler_.IsValid() ? 1u : 0u;
        count += quad_vertices_.IsValid() ? 1u : 0u;
        count += quad_indices_.IsValid() ? 1u : 0u;
        count += dot_mask_.IsValid() ? 1u : 0u;
        count += output_target_.IsValid() ? 1u : 0u;
        return count;
    }

    void PanelRenderer::DestroyDotMask() noexcept
    {
        if (backend_ != nullptr && dot_mask_.IsValid())
        {
            backend_->DestroyTexture(dot_mask_);
        }
        dot_mask_ = {};
    }

    void PanelRenderer::DestroyOutputTarget() noexcept
    {
        if (backend_ != nullptr && output_target_.IsValid())
        {
            backend_->DestroyRenderTarget(output_target_);
        }
        output_target_ = {};
        output_view_ = {};
        output_width_ = 0u;
        output_height_ = 0u;
    }

    void PanelRenderer::DestroyQuadGeometry() noexcept
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

    void PanelRenderer::DestroyPipeline() noexcept
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

    void PanelRenderer::Cleanup() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->WaitIdle();
        }
        // Reverse creation order: the mask and target the draws reference go
        // first, then the geometry, then the sampler and pipeline that consume
        // them.
        DestroyDotMask();
        DestroyOutputTarget();
        DestroyQuadGeometry();
        DestroyPipeline();
        initialized_ = false;
        backend_ = nullptr;
    }
}
