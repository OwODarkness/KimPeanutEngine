#include "renderer_frame_targets.h"

#include <array>

#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/texture.h"

namespace kpengine::render
{
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
            desc.depth = graphics::RenderTargetDepthAttachment{};
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
