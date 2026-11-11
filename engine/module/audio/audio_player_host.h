#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_HOST_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_HOST_H

#include <memory>
#include <string>
#include <vector>

#include "runtime/host/application_host.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/command/command_registry.h"

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::audio_player
{
    class AudioPlayerEditor;
    class AudioPlayerController;

    class AudioPlayerHost final : public runtime::IApplicationHost
    {
    public:
        AudioPlayerHost();
        ~AudioPlayerHost() override;

        const char *Name() const noexcept override { return "AudioPlayerHost"; }
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
        std::unique_ptr<audio::MiniAudioSystem> audio_system_;
        std::unique_ptr<AudioPlayerController> controller_;
        std::unique_ptr<AudioPlayerEditor> player_editor_;
        std::vector<runtime::command::CommandRegistration> command_registrations_;
        std::vector<asset::AssetID> render_asset_roots_;
        bool initialized_ = false;
    };
}

#endif
