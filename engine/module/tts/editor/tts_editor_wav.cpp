#include "tts_editor_wav.h"

#include <cstring>
#include <limits>
#include <algorithm>
#include <cmath>

namespace kpengine::tts_editor
{
    namespace
    {
        std::uint16_t Read16(const std::uint8_t *data)
        {
            return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
        }

        std::uint32_t Read32(const std::uint8_t *data)
        {
            return static_cast<std::uint32_t>(data[0]) |
                (static_cast<std::uint32_t>(data[1]) << 8) |
                (static_cast<std::uint32_t>(data[2]) << 16) |
                (static_cast<std::uint32_t>(data[3]) << 24);
        }

        void Write16(std::uint8_t *data, std::uint16_t value)
        {
            data[0] = static_cast<std::uint8_t>(value);
            data[1] = static_cast<std::uint8_t>(value >> 8);
        }

        void Write32(std::uint8_t *data, std::uint32_t value)
        {
            for (std::size_t index = 0; index < 4; ++index)
                data[index] = static_cast<std::uint8_t>(value >> (index * 8));
        }
    }

    bool CanonicalizeWav(std::span<const std::uint8_t> input,
                         std::vector<std::uint8_t> &output,
                         std::string &diagnostic)
    {
        output.clear();
        if (input.size() < 44 || input.size() > kMaximumWavBytes ||
            std::memcmp(input.data(), "RIFF", 4) != 0 ||
            std::memcmp(input.data() + 8, "WAVE", 4) != 0)
        {
            diagnostic = "Expected a WAV file of at most 32 MiB";
            return false;
        }

        std::uint16_t channels = 0;
        std::uint32_t sample_rate = 0;
        std::size_t data_offset = 0;
        std::size_t data_size = 0;
        std::size_t offset = 12;
        while (offset <= input.size() && input.size() - offset >= 8)
        {
            const auto *chunk = input.data() + offset;
            const std::uint32_t length = Read32(chunk + 4);
            const std::size_t payload = offset + 8;
            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                if (length < 16 || length > input.size() - payload ||
                    Read16(chunk + 8) != 1 || Read16(chunk + 22) != 16)
                {
                    diagnostic = "WAV must contain PCM16 format";
                    return false;
                }
                channels = Read16(chunk + 10);
                sample_rate = Read32(chunk + 12);
                if ((channels != 1 && channels != 2) ||
                    sample_rate < 8000 || sample_rate > 192000 ||
                    Read16(chunk + 20) != channels * 2 ||
                    Read32(chunk + 16) != sample_rate * channels * 2)
                {
                    diagnostic = "WAV format fields are inconsistent";
                    return false;
                }
            }
            if (std::memcmp(chunk, "data", 4) == 0)
            {
                if (channels == 0)
                {
                    diagnostic = "WAV data precedes its format";
                    return false;
                }
                data_offset = payload;
                data_size = length == 0 || length == std::numeric_limits<std::uint32_t>::max()
                    ? input.size() - payload : length;
                if (data_size > input.size() - payload || data_size == 0 ||
                    data_size % (channels * 2) != 0 ||
                    data_size > kMaximumWavBytes - 44)
                {
                    diagnostic = "WAV data is truncated, unaligned, or too large";
                    return false;
                }
                break;
            }
            if (length > input.size() - payload ||
                static_cast<std::size_t>(length) + (length & 1u) > input.size() - payload)
            {
                diagnostic = "WAV chunk is truncated";
                return false;
            }
            offset = payload + length + (length & 1u);
        }
        if (data_size == 0)
        {
            diagnostic = "WAV has no audio data";
            return false;
        }

        output.resize(44 + data_size);
        std::memcpy(output.data(), "RIFF", 4);
        Write32(output.data() + 4, static_cast<std::uint32_t>(output.size() - 8));
        std::memcpy(output.data() + 8, "WAVEfmt ", 8);
        Write32(output.data() + 16, 16);
        Write16(output.data() + 20, 1);
        Write16(output.data() + 22, channels);
        Write32(output.data() + 24, sample_rate);
        Write32(output.data() + 28, sample_rate * channels * 2);
        Write16(output.data() + 32, channels * 2);
        Write16(output.data() + 34, 16);
        std::memcpy(output.data() + 36, "data", 4);
        Write32(output.data() + 40, static_cast<std::uint32_t>(data_size));
        std::memcpy(output.data() + 44, input.data() + data_offset, data_size);
        diagnostic.clear();
        return true;
    }

    bool BuildWavPreviewData(const std::span<const std::uint8_t> input,
                             WavPreviewData &preview,
                             std::string &diagnostic,
                             std::size_t sample_count)
    {
        preview = {};
        std::vector<std::uint8_t> canonical;
        if (!CanonicalizeWav(input, canonical, diagnostic))
            return false;

        preview.channels = Read16(canonical.data() + 22);
        preview.sample_rate = Read32(canonical.data() + 24);
        const std::size_t data_size = Read32(canonical.data() + 40);
        const std::size_t frame_size = static_cast<std::size_t>(preview.channels) * 2;
        const std::size_t frame_count = data_size / frame_size;
        if (frame_count == 0 || sample_count == 0)
        {
            preview = {};
            diagnostic = "WAV has no samples for waveform preview";
            return false;
        }

        preview.duration_seconds = static_cast<float>(frame_count) /
            static_cast<float>(preview.sample_rate);
        const std::size_t bucket_count = std::min(sample_count, frame_count);
        preview.waveform_samples.resize(bucket_count);
        for (std::size_t bucket = 0; bucket < bucket_count; ++bucket)
        {
            const std::size_t first_frame = bucket * frame_count / bucket_count;
            const std::size_t last_frame = std::max(first_frame + 1,
                (bucket + 1) * frame_count / bucket_count);
            float peak = 0.0f;
            for (std::size_t frame = first_frame; frame < last_frame; ++frame)
            {
                for (std::size_t channel = 0; channel < preview.channels; ++channel)
                {
                    const std::size_t sample_offset = 44 + frame * frame_size + channel * 2;
                    std::int32_t sample = static_cast<std::int32_t>(
                        Read16(canonical.data() + sample_offset));
                    if (sample >= 32768)
                        sample -= 65536;
                    peak = std::max(peak, static_cast<float>(std::abs(sample)) / 32768.0f);
                }
            }
            preview.waveform_samples[bucket] = peak;
        }
        diagnostic.clear();
        return true;
    }
}
