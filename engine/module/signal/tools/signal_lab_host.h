#ifndef KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_LAB_HOST_H
#define KPENGINE_MODULE_SIGNAL_TOOLS_SIGNAL_LAB_HOST_H

#include <memory>
#include <string>
#include <vector>

#include "runtime/command/command_registry.h"
#include "runtime/host/application_host.h"

namespace kpengine::signal
{
    class SignalLabEditor;

    class SignalLabHost final : public runtime::IApplicationHost
    {
    public:
        SignalLabHost();
        ~SignalLabHost() override;

        const char *Name() const noexcept override { return "SignalLabHost"; }
        bool Initialize(runtime::Engine &engine, std::string &diagnostic) override;
        bool InitializePresentation(runtime::Engine &engine, std::string &diagnostic) override;
        bool Tick(float delta_time, std::string &diagnostic) override;
        bool RecordFrame(std::string &diagnostic) override;
        bool RenderPresentation(std::string &diagnostic) override;
        bool RegisterHostCommands(runtime::command::CommandRegistry &registry,
                                  std::string &diagnostic) override;
        std::vector<asset::AssetID> GetRenderAssetRoots() const override
        {
            return render_asset_roots_;
        }
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        std::unique_ptr<SignalLabEditor> editor_;
        std::vector<asset::AssetID> render_asset_roots_;
        bool initialized_ = false;
    };
}

#endif
