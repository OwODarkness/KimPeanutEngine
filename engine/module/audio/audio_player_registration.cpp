#include "module/module_registration.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "runtime/engine.h"
#include "audio_player_host.h"

namespace kpengine::audio_player
{
    namespace
    {
        void RegisterAudioPlayer(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() != runtime::ApplicationMode::AudioPlayer)
            {
                return;
            }

            std::string diagnostic;
            if (!engine.RegisterApplicationHostProvider(
                    runtime::ApplicationMode::AudioPlayer,
                    [](runtime::Engine &) { return std::make_unique<AudioPlayerHost>(); },
                    diagnostic))
            {
                throw std::runtime_error("Audio player host registration failed: " + diagnostic);
            }
        }

        const module::ModuleRegistration registration(&RegisterAudioPlayer);
    }
}
