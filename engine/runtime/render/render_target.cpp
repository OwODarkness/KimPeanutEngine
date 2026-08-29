#include "render_target.h"

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    struct RenderTarget::Impl
    {
        graphics::RenderBackend *backend = nullptr;
        graphics::RenderTargetHandle handle;
        graphics::RenderTargetDesc desc{};
        uint32_t width = 0;
        uint32_t height = 0;
    };

    RenderTarget::RenderTarget() : impl_(std::make_unique<Impl>()) {}
    RenderTarget::~RenderTarget() = default;

    bool RenderTarget::IsValid() const
    {
        return impl_ && impl_->handle.IsValid();
    }

    uint32_t RenderTarget::GetWidth() const { return impl_ ? impl_->width : 0; }
    uint32_t RenderTarget::GetHeight() const { return impl_ ? impl_->height : 0; }
    graphics::RenderTargetHandle RenderTarget::GetHandle() const
    {
        return impl_ ? impl_->handle : graphics::RenderTargetHandle{};
    }

    graphics::RenderTargetView RenderTarget::GetView() const
    {
        if (!IsValid() || !impl_->backend)
        {
            return {};
        }
        return impl_->backend->GetRenderTargetView(impl_->handle);
    }

    void RenderTarget::Initialize(graphics::RenderBackend &backend,
                                  const graphics::RenderTargetDesc &desc)
    {
        Cleanup();
        if (desc.width == 0 || desc.height == 0)
        {
            return;
        }
        impl_->backend = &backend;
        impl_->desc = desc;
        impl_->handle = backend.CreateRenderTarget(desc);
        if (impl_->handle.IsValid())
        {
            impl_->width = desc.width;
            impl_->height = desc.height;
        }
        else
        {
            impl_->backend = nullptr;
            impl_->desc = {};
        }
    }

    uint32_t RenderTarget::GetColorAttachmentCount() const
    {
        return impl_ ? static_cast<uint32_t>(impl_->desc.color_attachments.size()) : 0;
    }

    graphics::TextureHandle RenderTarget::GetColorAttachmentTexture(uint32_t index) const
    {
        if (!IsValid() || !impl_->backend)
        {
            return {};
        }
        return impl_->backend->GetRenderTargetColorAttachment(impl_->handle, index);
    }

    graphics::TextureHandle RenderTarget::GetDepthTexture() const
    {
        if (!IsValid() || !impl_->backend)
        {
            return {};
        }
        return impl_->backend->GetRenderTargetDepthAttachment(impl_->handle);
    }

    graphics::TextureHandle RenderTarget::GetSampledDepthTexture() const
    {
        if (!IsValid() || !impl_->backend)
        {
            return {};
        }
        return impl_->backend->GetRenderTargetSampledDepthAttachment(impl_->handle);
    }

    void RenderTarget::Cleanup()
    {
        if (impl_ && impl_->backend && impl_->handle.IsValid())
        {
            impl_->backend->DestroyRenderTarget(impl_->handle);
        }
        if (impl_)
        {
            impl_->backend = nullptr;
            impl_->handle = {};
            impl_->desc = {};
            impl_->width = 0;
            impl_->height = 0;
        }
    }

    bool RenderTarget::BeginRecording(graphics::CommandRecorder &recorder) const
    {
        if (!IsValid())
        {
            return false;
        }
        return recorder.BeginRenderTarget(impl_->handle);
    }

    void RenderTarget::EndRecording(graphics::CommandRecorder &recorder) const
    {
        if (IsValid())
        {
            recorder.EndRenderTarget();
        }
    }
}
