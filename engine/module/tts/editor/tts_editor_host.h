#ifndef KPENGINE_MODULE_TTS_EDITOR_HOST_H
#define KPENGINE_MODULE_TTS_EDITOR_HOST_H

#include <memory>
#include <vector>

#include "runtime/host/application_host.h"
#include "runtime/command/command_registry.h"

namespace kpengine::tts_editor
{
    class TtsEditorController;
    class TtsEditorEditor;

    class TtsEditorHost final : public runtime::IApplicationHost
    {
    public:
        TtsEditorHost();
        ~TtsEditorHost() override;

        const char *Name() const noexcept override { return "TtsEditorHost"; }
        bool Initialize(runtime::Engine &engine, std::string &diagnostic) override;
        bool InitializePresentation(runtime::Engine &engine,
                                    std::string &diagnostic) override;
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
        std::unique_ptr<TtsEditorController> controller_;
        std::unique_ptr<TtsEditorEditor> editor_;
        std::vector<runtime::command::CommandRegistration> command_registrations_;
        std::vector<asset::AssetID> render_asset_roots_;
        bool initialized_ = false;
    };
}

#endif
