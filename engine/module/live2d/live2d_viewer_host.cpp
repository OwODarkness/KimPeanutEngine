#include "live2d_viewer_host.h"

#include <algorithm>
#include <exception>

#include "asset/asset_manager.h"
#include "config/path.h"
#include "engine.h"
#include "graphics/backend/common/render_backend.h"
#include "live2d_settings.h"
#include "module/live2d/render/live2d_renderer.h"
#include "module/live2d/runtime/live2d_model_resource.h"
#include "window/window_system.h"

namespace kpengine::live2d
{
    bool Live2DViewerHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::Live2DViewer)
        {
            diagnostic = "Live2D viewer host can only initialize in live2d-viewer mode";
            return false;
        }

        try
        {
            engine_ = &engine;
            const Live2DSettings settings = ReadLive2DSettings(
                ComposePath(project_root, "config/live2d.json"));
            if (!settings.enabled)
            {
                diagnostic = "Live2D viewer is disabled by config/live2d.json";
                return false;
            }

            const std::string model_path = GetAssetDirectory() + settings.preview_asset;
            model_asset_ = asset::AssetManager::GetInstance().LoadSync(model_path);
            if (!model_asset_.IsValid() || model_asset_.type != kLive2DModelAssetType)
            {
                diagnostic = "Configured Live2D product could not be loaded: " + model_path;
                return false;
            }

            window_ = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
            if (!window_)
            {
                diagnostic = "Live2D viewer could not create a window system";
                return false;
            }
            WindowCreateInfo window_info{};
            window_info.width = 720;
            window_info.height = 960;
            window_info.title = "KimPeanut Live2D Viewer";
            window_info.graphics_api_type = engine.GetGraphicsAPI();
            if (!window_->Initialize(window_info))
            {
                diagnostic = "Live2D viewer window initialization failed";
                return false;
            }
            window_initialized_ = true;

            backend_ = graphics::RenderBackend::CreateGraphicsBackEnd(engine.GetGraphicsAPI());
            if (!backend_)
            {
                diagnostic = "Live2D viewer could not create its graphics backend";
                return false;
            }
            backend_->BindWindowResize(window_->resize_event_dispatcher_);
            backend_->Initialize(window_->GetNativeHandle());
            backend_initialized_ = true;

            const uint32_t frame_count = std::max(1u, backend_->GetFramesInFlight());
            frame_contexts_.reserve(frame_count);
            for (uint32_t index = 0; index < frame_count; ++index)
            {
                auto frame = std::make_unique<render::FrameContext>();
                frame->Initialize(*backend_, 4u * 1024u * 1024u);
                frame_contexts_.push_back(std::move(frame));
            }

            if (!system_.Initialize())
            {
                diagnostic = "Live2D Cubism framework initialization failed";
                return false;
            }
            system_initialized_ = true;
            renderer_ = std::make_unique<Live2DRenderer>(system_, model_asset_);
            renderer_->SetPresentationTarget(true);
            if (!renderer_->Initialize(*backend_, window_info.width, window_info.height,
                                       diagnostic))
            {
                return false;
            }
            render_initialized_ = true;
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return false;
        }
        catch (...)
        {
            diagnostic = "unknown Live2D viewer initialization failure";
            return false;
        }
    }

    bool Live2DViewerHost::Tick(const float delta_time, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!system_initialized_)
        {
            diagnostic = "Live2D viewer system is not initialized";
            return false;
        }
        system_.Tick(delta_time);
        return true;
    }

    bool Live2DViewerHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!render_initialized_ || !window_ || !backend_ || frame_contexts_.empty() ||
            !renderer_)
        {
            diagnostic = "Live2D viewer render state is not initialized";
            return false;
        }
        window_->PollEvents();
        if (window_->ShouldClose())
        {
            return true;
        }

        backend_->BeginFrame();
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        if (recorder == nullptr)
        {
            return true;
        }
        const uint32_t frame_index = backend_->GetCurrentFrameIndex() %
                                     static_cast<uint32_t>(frame_contexts_.size());
        render::FrameContext &frame = *frame_contexts_[frame_index];
        const graphics::Extent2D extent = backend_->GetRenderExtent();
        elapsed_seconds_ += 1.0f / 120.0f;
        frame.Begin(frame_index, {frame_number_++, elapsed_seconds_, 1.0f / 120.0f}, extent);
        if (!renderer_->Record(frame, *recorder, 1.0f / 120.0f, diagnostic))
        {
            frame.End();
            backend_->EndFrame();
            return false;
        }
        frame.End();
        backend_->EndFrame();
        if (engine_->GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            window_->SwapBuffers();
        }
        return true;
    }

    bool Live2DViewerHost::ShouldClose() const noexcept
    {
        return window_ != nullptr && window_->ShouldClose();
    }

    void Live2DViewerHost::CleanupGpu() noexcept
    {
        if (backend_ && backend_initialized_)
        {
            backend_->WaitIdle();
        }
        renderer_.reset();
        for (const std::unique_ptr<render::FrameContext> &frame : frame_contexts_)
        {
            if (frame)
            {
                frame->Cleanup();
            }
        }
        frame_contexts_.clear();
        if (backend_)
        {
            if (backend_initialized_)
            {
                backend_->Cleanup();
            }
            backend_.reset();
        }
        if (window_)
        {
            if (window_initialized_)
            {
                window_->Cleanup();
            }
            window_.reset();
        }
        render_initialized_ = false;
        backend_initialized_ = false;
        window_initialized_ = false;
    }

    void Live2DViewerHost::ShutdownRenderThread() noexcept
    {
        CleanupGpu();
    }

    void Live2DViewerHost::Shutdown() noexcept
    {
        CleanupGpu();
        if (system_initialized_)
        {
            system_.Shutdown();
            system_initialized_ = false;
        }
        model_asset_ = {};
        engine_ = nullptr;
    }
}
