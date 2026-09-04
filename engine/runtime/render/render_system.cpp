#include "render_system.h"

#include <stdexcept>
#include <utility>

#include "asset/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/render_capture_service_internal.h"
#include "render_resource_resolver.h"

namespace kpengine::render
{
    namespace
    {
        constexpr const char *GetGraphicsApiName(GraphicsAPIType api_type)
        {
            switch (api_type)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL:
                return "OpenGL";
            case GraphicsAPIType::GRAPHICS_API_VULKAN:
                return "Vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW:
            default:
                return "Unknown";
            }
        }

    }

    RenderSystem::RenderSystem() = default;

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    RenderSystemInitResult RenderSystem::Initialize(const RenderSystemInitInfo &info)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::Uninitialized)
        {
            last_diagnostic_ = "RenderSystem can only be initialized once.";
            return {false, last_diagnostic_};
        }
        if (!info.native_window || !info.resize_dispatcher || !info.prepared_assets)
        {
            last_diagnostic_ =
                "RenderSystem initialization requires window, resize dispatcher, and prepared assets.";
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", last_diagnostic_.c_str());
            return {false, last_diagnostic_};
        }
        try
        {
            prepared_assets_ = info.prepared_assets;
            material_system_ = std::make_unique<MaterialSystem>();

            RenderBackendFactory factory = info.backend_factory;
            if (!factory)
            {
                factory = [](GraphicsAPIType api_type)
                { return graphics::RenderBackend::CreateGraphicsBackEnd(api_type); };
            }
            backend_ = factory(info.api_type);
            if (!backend_)
            {
                throw std::runtime_error("No graphics backend is available for the requested API.");
            }
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "RenderSystem selected %s graphics backend",
                   GetGraphicsApiName(info.api_type));
            backend_->BindWindowResize(*info.resize_dispatcher);
            backend_->Initialize(info.native_window);
            backend_initialized_ = true;

            resource_resolver_ = std::make_unique<RenderResourceResolver>(
                *backend_, *info.prepared_assets);
            material_system_->SetResourceResolver(resource_resolver_.get());
            scene_coordinator_.Bind(*material_system_, *resource_resolver_, prepared_assets_);
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "Prepared render catalog contains %u shader(s)",
                   static_cast<unsigned>(prepared_assets_->GetPreparedShaderCount()));

            constexpr size_t kFrameUniformCapacity = 64 * 1024;
            frame_contexts_.resize(backend_->GetFramesInFlight());
            for (FrameContext &context : frame_contexts_)
            {
                context.Initialize(*backend_, kFrameUniformCapacity);
            }

            const graphics::Extent2D extent = backend_->GetRenderExtent();
            deferred_renderer_ = std::make_unique<DeferredRenderer>();
            const DeferredRendererInitResult renderer_result = deferred_renderer_->Initialize(
                {*backend_, *resource_resolver_, *material_system_, *info.prepared_assets},
                extent.width, extent.height);
            if (!renderer_result)
            {
                throw std::runtime_error(renderer_result.diagnostic);
            }
            render_capture_service_ = std::make_unique<RenderCaptureService>(
                backend_->GetRenderTargetReadback(),
                [this](CaptureView view)
                {
                    return deferred_renderer_ ? deferred_renderer_->GetCaptureTarget(view)
                                               : graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            lifecycle_state_ = RenderSystemLifecycleState::Ready;
            last_diagnostic_.clear();
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            last_diagnostic_ = error.what();
        }
        catch (...)
        {
            last_diagnostic_ = "Unknown exception during RenderSystem initialization.";
        }
        KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem initialization failed: %s",
               last_diagnostic_.c_str());
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::Uninitialized;
        return {false, last_diagnostic_};
    }

    bool RenderSystem::BeginFrame(float delta_time)
    {
        if (!IsState(RenderSystemLifecycleState::Ready))
        {
            return false;
        }
        if (!deferred_renderer_)
        {
            return false;
        }
        RenderSceneFrameInput input = scene_coordinator_.PrepareFrame(
            render_capture_service_ ? render_capture_service_->GetPendingView()
                                    : std::nullopt);
        deferred_renderer_->ApplyPendingExtent();
        backend_->BeginFrame();
        active_frame_context_ = GetCurrentFrameContext();
        if (!active_frame_context_)
        {
            backend_->EndFrame();
            active_frame_context_ = nullptr;
            last_diagnostic_ = "Graphics backend opened a frame without a valid frame context.";
            return false;
        }
        lifecycle_state_ = RenderSystemLifecycleState::FrameActive;
        elapsed_seconds_ += delta_time;
        active_frame_context_->Begin(
            backend_->GetCurrentFrameIndex(),
            {frame_number_, elapsed_seconds_, delta_time},
            {deferred_renderer_->GetSceneRenderTarget().GetWidth(),
             deferred_renderer_->GetSceneRenderTarget().GetHeight()});
        const DeferredRendererFrameResult result =
            deferred_renderer_->RecordFrame(*active_frame_context_, input);
        if (input.pending_capture.has_value())
        {
            if (!result.capture_target_ready)
            {
                render_capture_service_->RejectPendingCapture(
                    "Render could not record the requested capture-view conversion pass");
            }
            else
            {
                render_capture_service_->EnqueuePendingReadback();
            }
        }
        return true;
    }

    bool RenderSystem::EndFrame()
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive))
        {
            return false;
        }
        if (active_frame_context_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            active_frame_context_->End();
            active_frame_context_ = nullptr;
            ++frame_number_;
        }
        backend_->EndFrame();
        lifecycle_state_ = RenderSystemLifecycleState::Ready;
        return true;
    }

    bool RenderSystem::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive) || !active_frame_context_ ||
            !record_pass || !deferred_renderer_)
        {
            return false;
        }
        return deferred_renderer_->ExecuteEditorCompositePass(record_pass);
    }

    void RenderSystem::RequestSceneRenderTargetExtent(uint32_t width, uint32_t height)
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::Uninitialized ||
            lifecycle_state_ == RenderSystemLifecycleState::ShutDown || width == 0 || height == 0)
        {
            return;
        }
        if (deferred_renderer_)
        {
            deferred_renderer_->RequestExtent(width, height);
        }
    }

    graphics::RenderTargetView RenderSystem::GetSceneRenderTargetView() const
    {
        return deferred_renderer_ ? deferred_renderer_->GetSceneRenderTarget().GetView()
                                   : graphics::RenderTargetView{};
    }

    RenderSystem::RenderSystemMetrics RenderSystem::GetMetrics() const
    {
        return {prepared_assets_ != nullptr
                    ? static_cast<uint32_t>(prepared_assets_->GetPreparedShaderCount())
                    : 0};
    }

    graphics::IEditorPresentationBridge *RenderSystem::GetEditorPresentationBridge()
    {
        return backend_ ? backend_->GetEditorPresentationBridge() : nullptr;
    }

    IRenderCaptureService *RenderSystem::GetRenderCaptureService()
    {
        return render_capture_service_.get();
    }

    FrameContext *RenderSystem::GetCurrentFrameContext()
    {
        if (!backend_ || !backend_->GetCommandRecorder())
        {
            return nullptr;
        }
        const uint32_t index = backend_->GetCurrentFrameIndex();
        return index < frame_contexts_.size() ? &frame_contexts_[index] : nullptr;
    }

    bool RenderSystem::IsState(RenderSystemLifecycleState expected) const
    {
        return lifecycle_state_ == expected;
    }

    void RenderSystem::CleanupOwnedState()
    {
        // If teardown is requested between BeginFrame and EndFrame, close the
        // frame bracket before waiting or destroying any frame-owned resource.
        if (lifecycle_state_ == RenderSystemLifecycleState::FrameActive && backend_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            if (active_frame_context_)
            {
                active_frame_context_->End();
                active_frame_context_ = nullptr;
            }
            backend_->EndFrame();
        }

        scene_coordinator_.Clear();
        material_system_.reset();

        if (backend_ && backend_initialized_)
        {
            // Releasing material instances first retires their bindless table
            // slots. WaitIdle then makes all submitted GPU work safe to retire.
            backend_->WaitIdle();
            if (graphics::IRenderTargetReadback *const readback =
                    backend_->GetRenderTargetReadback())
            {
                readback->DrainPendingReadbacks("Render system shutdown");
            }
        }
        render_capture_service_.reset();

        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        if (deferred_renderer_)
        {
            deferred_renderer_->Cleanup();
            deferred_renderer_.reset();
        }

        if (resource_resolver_)
        {
            resource_resolver_->Cleanup();
            resource_resolver_.reset();
        }
        if (backend_)
        {
            backend_->Cleanup();
            backend_.reset();
        }
        backend_initialized_ = false;
        prepared_assets_.reset();
        active_frame_context_ = nullptr;
        frame_number_ = 0;
        elapsed_seconds_ = 0.0f;
    }

    void RenderSystem::Shutdown()
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::ShutDown)
        {
            return;
        }
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::ShutDown;
    }
}
