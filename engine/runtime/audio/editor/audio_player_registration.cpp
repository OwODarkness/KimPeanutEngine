#include <memory>

#include "audio_player_host.h"
#include "runtime/host/application_host.h"

namespace kpengine::audio_player
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"audio-player",
             [](runtime::Engine &) { return std::make_unique<AudioPlayerHost>(); },
             runtime::ApplicationHostLayout::SharedWindowPresentation,
             false}};
    }
}
