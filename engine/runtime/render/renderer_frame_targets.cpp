#include "renderer_frame_targets.h"

#include <array>

#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/texture.h"

namespace kpengine::render
{
    namespace
    {
        constexpr uint32_t kDirectionalShadowResolution = 2048;
        constexpr uint32_t kSpotShadowResolution = 1024;
        constexpr uint32_t kPointShadowFaceResolution = 512;
    }
    struct RendererFrameTargets::Impl
    {
        graphics::RenderBackend *backend = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        std::array<std::unique_ptr<RenderTarget>, kRenderTargetNameCount> targets;
    };

    RendererFrameTargets::RendererFrameTargets() : impl_(std::make_unique<Impl>())
    {
        for (auto &target : impl_->targets)
        {
            target = std::make_unique<RenderTarget>();
        }
    }

    RendererFrameTargets::~RendererFrameTargets() = default;

    graphics::RenderTargetDesc RendererFrameTargets::BuildDesc(RenderTargetName name,
                                                               uint32_t width,
                                                               uint32_t height) const
    {
        graphics::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        switch (name)
        {
        case RenderTargetName::SceneColor:
            // Final composite color only. The D3 debug pass writes this target
            // with a no-depth pipeline, so a depth attachment here would violate
            // target/pipeline compatibility on Vulkan. The depth that lived on
            // this target belonged to the retired unlit ScenePass.
            desc.color_attachments = {{graphics::RenderTargetColorAttachment{}}};
            break;
        case RenderTargetName::GBuffer:
            // Deferred G-buffer: linear albedo, raw world-space normal, packed
            // material params, plus depth. Encodings mirror the plan's G-buffer
            // table; the GBufferPass pipeline must declare the same formats.
            desc.color_attachments = {
                {graphics::RenderTargetColorAttachment{
                     TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                     graphics::RenderTargetLoadOp::Clear,
                     graphics::RenderTargetStoreOp::Store,
                     {0.f, 0.f, 0.f, 0.f}}},
                {graphics::RenderTargetColorAttachment{
                     TextureFormat::TEXTURE_FORMAT_RGBA16F,
                     graphics::RenderTargetLoadOp::Clear,
                     graphics::RenderTargetStoreOp::Store,
                     {0.f, 0.f, 1.f, 0.f}}},
                {graphics::RenderTargetColorAttachment{
                     TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                     graphics::RenderTargetLoadOp::Clear,
                     graphics::RenderTargetStoreOp::Store,
                     {0.f, 1.f, 1.f, 0.f}}},
            };
            desc.depth = graphics::RenderTargetDepthAttachment{
                TextureFormat::TEXTURE_FORMAT_D32,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store,
                1.0f,
                0,
                true};
            break;
        case RenderTargetName::DirectionalShadow:
            // Fixed-resolution depth producer. It is sampled only by
            // Render-owned consumers, never by Gameplay or Asset.
            desc.width = kDirectionalShadowResolution;
            desc.height = kDirectionalShadowResolution;
            desc.depth = graphics::RenderTargetDepthAttachment{
                TextureFormat::TEXTURE_FORMAT_D32,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store,
                1.0f,
                0,
                true};
            break;
        case RenderTargetName::SpotShadow:
            // Fixed-budget punctual shadow producer. Render owns the policy;
            // Graphics owns allocation and sampled-depth transitions.
            desc.width = kSpotShadowResolution;
            desc.height = kSpotShadowResolution;
            desc.depth = graphics::RenderTargetDepthAttachment{
                TextureFormat::TEXTURE_FORMAT_D32,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store,
                1.0f,
                0,
                true};
            break;
        case RenderTargetName::PointShadow:
            // Six canonical 512² faces packed as a fixed 3×2 sampled D32
            // atlas. The target remains an ordinary 2D attachment so both
            // backends use the existing render-target contract.
            desc.width = kPointShadowFaceResolution * 3;
            desc.height = kPointShadowFaceResolution * 2;
            desc.depth = graphics::RenderTargetDepthAttachment{
                TextureFormat::TEXTURE_FORMAT_D32,
                graphics::RenderTargetLoadOp::Clear,
                graphics::RenderTargetStoreOp::Store,
                1.0f,
                0,
                true};
            break;
        case RenderTargetName::SceneHdr:
            // Linear HDR lighting/debug output. ToneMapPass is the only normal
            // presentation consumer; SceneColor remains the stable LDR target.
            desc.color_attachments = {
                {graphics::RenderTargetColorAttachment{
                    TextureFormat::TEXTURE_FORMAT_RGBA16F,
                    graphics::RenderTargetLoadOp::Clear,
                    graphics::RenderTargetStoreOp::Store,
                    {0.f, 0.f, 0.f, 1.f}}},
            };
            break;
        case RenderTargetName::CaptureOutput:
            // Conditional diagnostic conversion output. Every semantic capture
            // is converted to displayable RGBA8 before Graphics readback.
            desc.color_attachments = {{graphics::RenderTargetColorAttachment{}}};
            break;
        case RenderTargetName::Count:
            break;
        }
        return desc;
    }

    void RendererFrameTargets::Initialize(graphics::RenderBackend &backend, uint32_t width,
                                          uint32_t height)
    {
        Cleanup();
        if (width == 0 || height == 0)
        {
            return;
        }
        impl_->backend = &backend;
        impl_->width = width;
        impl_->height = height;
        for (uint32_t index = 0; index < kRenderTargetNameCount; ++index)
        {
            impl_->targets[index]->Initialize(
                backend, BuildDesc(static_cast<RenderTargetName>(index), width, height));
        }
    }

    void RendererFrameTargets::RebuildForExtent(graphics::RenderBackend &backend, uint32_t width,
                                                uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        if (impl_->backend == &backend && impl_->width == width && impl_->height == height)
        {
            return;
        }
        backend.WaitIdle();
        Initialize(backend, width, height);
    }

    void RendererFrameTargets::Cleanup()
    {
        if (!impl_)
        {
            return;
        }
        for (auto &target : impl_->targets)
        {
            target->Cleanup();
        }
        impl_->backend = nullptr;
        impl_->width = 0;
        impl_->height = 0;
    }

    bool RendererFrameTargets::IsValid() const
    {
        if (!impl_ || impl_->width == 0 || impl_->height == 0)
        {
            return false;
        }
        for (const auto &target : impl_->targets)
        {
            if (!target || !target->IsValid())
            {
                return false;
            }
        }
        return true;
    }

    RenderTarget *RendererFrameTargets::GetTarget(RenderTargetName name)
    {
        const uint32_t index = static_cast<uint32_t>(name);
        return index < kRenderTargetNameCount ? impl_->targets[index].get() : nullptr;
    }

    const RenderTarget *RendererFrameTargets::GetTarget(RenderTargetName name) const
    {
        const uint32_t index = static_cast<uint32_t>(name);
        return index < kRenderTargetNameCount ? impl_->targets[index].get() : nullptr;
    }
}
