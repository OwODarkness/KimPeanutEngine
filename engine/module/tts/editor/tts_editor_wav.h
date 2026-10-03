#ifndef KPENGINE_MODULE_TTS_EDITOR_WAV_H
#define KPENGINE_MODULE_TTS_EDITOR_WAV_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kpengine::tts_editor
{
    constexpr std::size_t kMaximumWavBytes = 32u * 1024u * 1024u;

    bool CanonicalizeWav(std::span<const std::uint8_t> input,
                         std::vector<std::uint8_t> &output,
                         std::string &diagnostic);
}

#endif
