#include <memory>

#include "audio_player_host.h"
#include "runtime/host/application_host.h"
#include "runtime/launch_options.h"

namespace kpengine::audio_player
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"audio-player",
             [](const runtime::RuntimeLaunchOptions &) { return std::make_unique<AudioPlayerHost>(); },
             runtime::ApplicationHostLayout::SharedWindowPresentation,
             false}};
    }
}
