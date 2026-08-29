#include "render_target.h"

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    struct RenderTarget::Impl
    {
        graphics::RenderBackend *backend = nullptr;
        graphics::RenderTargetHandle handle;
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

    void RenderTarget::Initialize(graphics::RenderBackend &backend, uint32_t width, uint32_t height)
    {
        Cleanup();
        if (width == 0 || height == 0)
        {
            return;
        }
        graphics::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        impl_->backend = &backend;
        impl_->handle = backend.CreateRenderTarget(desc);
        if (impl_->handle.IsValid())
        {
            impl_->width = width;
            impl_->height = height;
        }
        else
        {
            impl_->backend = nullptr;
        }
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
        recorder.BeginRenderTarget(impl_->handle);
        return true;
    }

    void RenderTarget::EndRecording(graphics::CommandRecorder &recorder) const
    {
        if (IsValid())
        {
            recorder.EndRenderTarget();
        }
    }
}
