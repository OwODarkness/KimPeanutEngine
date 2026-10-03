#ifndef KPENGINE_EDITOR_UI_AUDIO_TRANSPORT_ICONS_H
#define KPENGINE_EDITOR_UI_AUDIO_TRANSPORT_ICONS_H

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "editor/ui/audio_transport_strip.h"

namespace kpengine::editor
{
    enum class AudioTransportIcon : std::uint8_t
    {
        Previous,
        Play,
        Pause,
        Next,
        Stop,
        VoiceOpen,
        VoiceClosed,
        Loop,
        Count
    };

    struct AudioTransportIconMasks
    {
        static constexpr std::uint32_t Size = 24;
        std::array<std::vector<std::uint8_t>,
                   static_cast<std::size_t>(AudioTransportIcon::Count)> alpha;

        EditorControlIcon Get(AudioTransportIcon icon) const noexcept;
        TransportStripIcons GetTransportIcons() const noexcept;
    };

    AudioTransportIconMasks LoadAudioTransportIconMasks();
}

#endif
