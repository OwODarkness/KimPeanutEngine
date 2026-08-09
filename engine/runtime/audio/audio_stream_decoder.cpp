#include "audio_stream_decoder.h"
#include "log/logger.h"
namespace kpengine::audio
{
    static const char *LogName = "MiniAudio_AudioStreamDecoder";

    std::vector<float> ResampleLinear(const std::vector<float> &input,
                                      size_t input_sample_rate,
                                      size_t output_sample_rate)
    {
        if (input_sample_rate == output_sample_rate)
        {
            return input;
        }

        double ratio = static_cast<double>(output_sample_rate) / input_sample_rate;
        size_t output_size = static_cast<size_t>(input.size() * ratio);
        std::vector<float> output(output_size);

        for (size_t i = 0; i < output_size; i++)
        {
            double pos = i / ratio; // Position in input
            size_t idx = static_cast<size_t>(pos);
            double frac = pos - idx;

            if (idx >= input.size() - 1)
            {
                output[i] = static_cast<float>(input.back());
            }
            else
            {
                output[i] =  static_cast<float>(input[idx] * (1.0f - frac) + input[idx + 1] * frac);
            }
        }

        return output;
    }

    bool AudioStreamDecoder::Feed(const uint8_t *data, size_t size)
    {
        if (!stream_)
        {
            return false;
        }

        pending_bytes_.insert(
            pending_bytes_.end(),
            data,
            data + size);

        if (!header_parsed_)
        {
            header_parsed_ = ParseHeader();
            if (!header_parsed_)
            {
                return false;
            }
            // Fall through: the rest of this packet after the header is
            // audio, decode it right away so playback can start from the
            // very first chunk instead of waiting for the next one.
        }

        return DecodePCM();
    }

    void AudioStreamDecoder::Finish()
    {
        stream_->Finish();
    }

    bool AudioStreamDecoder::ParseHeader()
    {
        if (pending_bytes_.size() < 12)
        {
            return false;
        }

        const uint8_t *data = pending_bytes_.data();

        // RIFF
        if (memcmp(data, "RIFF", 4) != 0)
        {
            return false;
        }

        // WAVE
        if (memcmp(data + 8, "WAVE", 4) != 0)
        {
            return false;
        }

        size_t offset = 12;

        bool found_fmt = false;
        bool found_data = false;

        while (offset + 8 <= pending_bytes_.size())
        {
            const uint8_t *chunk = data + offset;

            uint32_t chunk_size =
                *reinterpret_cast<const uint32_t *>(chunk + 4);

            // fmt chunk
            if (memcmp(chunk, "fmt ", 4) == 0)
            {
                if (chunk_size < 16)
                {
                    return false;
                }

                const uint8_t *fmt = chunk + 8;

                wav_format_.audio_format =
                    *reinterpret_cast<const uint16_t *>(fmt + 0);

                wav_format_.channels =
                    *reinterpret_cast<const uint16_t *>(fmt + 2);

                wav_format_.sample_rate =
                    *reinterpret_cast<const uint32_t *>(fmt + 4);

                wav_format_.bits_per_sample =
                    *reinterpret_cast<const uint16_t *>(fmt + 14);

                found_fmt = true;
                KP_LOG("LogTemp", LOG_LEVEL_DEBUG, "Parse Header wave format %d, bits per sample %d, sample rate : %d, channels : %d", wav_format_.audio_format, wav_format_.bits_per_sample, wav_format_.sample_rate, wav_format_.channels);
            }

            // data chunk
            else if (memcmp(chunk, "data", 4) == 0)
            {
                data_offset_ =
                    offset + 8;

                found_data = true;

                break;
            }

            offset += 8 + chunk_size;
        }

        if (!found_fmt || !found_data)
        {
            return false;
        }

        // Remove WAV header from pending buffer
        pending_bytes_.erase(
            pending_bytes_.begin(),
            pending_bytes_.begin() + data_offset_);

        return true;
    }

bool AudioStreamDecoder::DecodePCM()
{
    const size_t bytes_per_sample =
        wav_format_.bits_per_sample / 8;

    if (bytes_per_sample != 2)
    {
        KP_LOG(LogName,
               LOG_LEVEL_ERROR,
               "Only 16-bit PCM is supported.");
        return false;
    }

    // Decode only complete samples.
    size_t bytes_to_decode =
        (pending_bytes_.size() / bytes_per_sample) * bytes_per_sample;

    if (bytes_to_decode == 0)
        return true;

    const int16_t* input =
        reinterpret_cast<const int16_t*>(pending_bytes_.data());

    size_t sample_count =
        bytes_to_decode / bytes_per_sample;

    std::vector<float> pcm(sample_count);

    for (size_t i = 0; i < sample_count; ++i)
    {
        pcm[i] = input[i] / 32768.0f;
    }

    const uint32_t input_rate = wav_format_.sample_rate;
    const uint32_t output_rate = stream_->GetAudioFormat().sample_rate;

    if (input_rate != output_rate)
    {
        pcm = ResampleLinear(
            pcm,
            input_rate,
            output_rate);

        sample_count = pcm.size();
    }

    stream_->PushFrames(
        pcm.data(),
        sample_count / wav_format_.channels);

    sample_total_ += sample_count / wav_format_.channels;

    pending_bytes_.erase(
        pending_bytes_.begin(),
        pending_bytes_.begin() + bytes_to_decode);



    return true;
}
}