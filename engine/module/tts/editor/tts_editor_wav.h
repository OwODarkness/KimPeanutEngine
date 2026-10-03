#ifndef KPENGINE_MODULE_TTS_EDITOR_WAV_H
#define KPENGINE_MODULE_TTS_EDITOR_WAV_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kpengine::tts_editor
{
    constexpr std::size_t kMaximumWavBytes = 32u * 1024u * 1024u;

    struct WavPreviewData
    {
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        float duration_seconds = 0.0f;
        std::vector<float> waveform_samples;
    };

    bool CanonicalizeWav(std::span<const std::uint8_t> input,
                         std::vector<std::uint8_t> &output,
                         std::string &diagnostic);
    bool BuildWavPreviewData(std::span<const std::uint8_t> input,
                             WavPreviewData &preview,
                             std::string &diagnostic,
                             std::size_t sample_count = 384);
}

#endif
