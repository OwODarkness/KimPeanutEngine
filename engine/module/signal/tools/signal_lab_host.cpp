#include "signal_lab_host.h"

#include "signal_lab_editor.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/level.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"

namespace kpengine::signal
{
    SignalLabHost::SignalLabHost() = default;

    SignalLabHost::~SignalLabHost()
    {
        Shutdown();
    }

    bool SignalLabHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != "signal-lab")
        {
            diagnostic = "SignalLabHost can only initialize in signal-lab mode";
            return false;
        }
        runtime::global_runtime_context.InitializeSceneServices();
        const asset::AssetID error_material = asset::AssetManager::GetInstance().LoadSync(
            GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath);
        if (!error_material.IsValid())
        {
            diagnostic = "Signal Lab could not load the Runtime error material root";
            return false;
        }
        render_asset_roots_ = {error_material};
        initialized_ = true;
        return true;
    }

    bool SignalLabHost::InitializePresentation(runtime::Engine &engine,
                                              std::string &diagnostic)
    {
        if (!initialized_)
        {
            diagnostic = "Signal Lab host is not initialized";
            return false;
        }
        editor_ = std::make_unique<SignalLabEditor>();
        if (!editor_->Initialize(engine, diagnostic))
        {
            editor_.reset();
            return false;
        }
        return true;
    }

    bool SignalLabHost::Tick(float, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_)
        {
            diagnostic = "Signal Lab host is not initialized";
            return false;
        }
        return true;
    }

    bool SignalLabHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        return initialized_;
    }

    bool SignalLabHost::RenderPresentation(std::string &diagnostic)
    {
        if (editor_ == nullptr)
        {
            diagnostic = "Signal Lab presentation is unavailable";
            return false;
        }
        return editor_->Render(diagnostic);
    }

    bool SignalLabHost::RegisterHostCommands(runtime::command::CommandRegistry &,
                                             std::string &diagnostic)
    {
        diagnostic.clear();
        return true;
    }

    void SignalLabHost::ShutdownRenderThread() noexcept
    {
        if (editor_ != nullptr)
        {
            editor_->Shutdown();
            editor_.reset();
        }
    }

    void SignalLabHost::Shutdown() noexcept
    {
        ShutdownRenderThread();
        render_asset_roots_.clear();
        initialized_ = false;
    }
}
