#include "audio_stream_decoder.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace kpengine::audio
{
    namespace
    {
        constexpr size_t kMaxHeaderBytes = 64 * 1024;

        uint16_t ReadU16LE(const uint8_t* bytes)
        {
            return static_cast<uint16_t>(bytes[0]) |
                   (static_cast<uint16_t>(bytes[1]) << 8);
        }

        uint32_t ReadU32LE(const uint8_t* bytes)
        {
            return static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
        }
    }

    AudioDecodeResult AudioStreamDecoder::Feed(const uint8_t* data, size_t size)
    {
        if (!stream_ || (!data && size > 0))
            return AudioDecodeResult::InvalidData;
        if (size == 0)
            return AudioDecodeResult::NeedMoreData;

        if (!header_parsed_ && size > kMaxHeaderBytes - pending_bytes_.size())
            return AudioDecodeResult::InvalidData;
        if (size > pending_bytes_.max_size() - pending_bytes_.size())
            return AudioDecodeResult::InvalidData;
        pending_bytes_.insert(pending_bytes_.end(), data, data + size);

        if (!header_parsed_)
        {
            const AudioDecodeResult header_result = ParseHeader();
            if (header_result != AudioDecodeResult::DataDecoded)
                return header_result;
            header_parsed_ = true;
        }
        return DecodePCM();
    }

    bool AudioStreamDecoder::Finish()
    {
        if (!stream_ || !header_parsed_ || !pending_bytes_.empty() ||
            (!data_size_unknown_ && data_bytes_remaining_ != 0))
            return false;

        const uint16_t output_channels = stream_->GetAudioFormat().channels;
        const size_t input_channels = wav_format_.channels;
        const size_t input_frame_count = resample_frames_.size() / input_channels;
        if (input_frame_count > 0)
        {
            std::vector<float> output;
            while (resample_position_ <= static_cast<double>(input_frame_count - 1))
            {
                const size_t a = static_cast<size_t>(resample_position_);
                const size_t b = std::min(a + 1, input_frame_count - 1);
                const float t = static_cast<float>(resample_position_ - a);
                float left = resample_frames_[a * input_channels] * (1.f - t) +
                             resample_frames_[b * input_channels] * t;
                float right = input_channels == 1 ? left :
                    resample_frames_[a * input_channels + 1] * (1.f - t) +
                    resample_frames_[b * input_channels + 1] * t;
                if (output_channels == 1)
                    output.push_back(input_channels == 1 ? left : (left + right) * 0.5f);
                else
                {
                    output.push_back(left);
                    output.push_back(right);
                }
                resample_position_ += static_cast<double>(wav_format_.sample_rate) /
                                      stream_->GetAudioFormat().sample_rate;
            }
            const size_t written = output.size() / output_channels;
            if (written && !stream_->PushFrames(output.data(), written))
                return false;
            output_frames_ += written;
        }
        if (output_frames_ == 0)
            return false;
        stream_->Finish();
        return true;
    }

    AudioDecodeResult AudioStreamDecoder::ParseHeader()
    {
        if (pending_bytes_.size() < 12)
            return AudioDecodeResult::NeedMoreData;
        if (std::memcmp(pending_bytes_.data(), "RIFF", 4) != 0 ||
            std::memcmp(pending_bytes_.data() + 8, "WAVE", 4) != 0)
            return AudioDecodeResult::InvalidData;

        bool found_fmt = false;
        size_t offset = 12;
        while (true)
        {
            if (offset > pending_bytes_.size() || pending_bytes_.size() - offset < 8)
                return AudioDecodeResult::NeedMoreData;
            const uint8_t* chunk = pending_bytes_.data() + offset;
            const uint32_t chunk_size = ReadU32LE(chunk + 4);
            const size_t payload = offset + 8;
            if (std::memcmp(chunk, "data", 4) == 0)
            {
                if (!found_fmt)
                    return AudioDecodeResult::InvalidData;
                data_offset_ = payload;
                data_size_unknown_ = chunk_size == std::numeric_limits<uint32_t>::max();
                data_bytes_remaining_ = data_size_unknown_ ? 0 : chunk_size;
                const size_t bytes_per_frame = (wav_format_.bits_per_sample / 8) * wav_format_.channels;
                if (!data_size_unknown_ && chunk_size % bytes_per_frame != 0)
                    return AudioDecodeResult::InvalidData;
                pending_bytes_.erase(pending_bytes_.begin(),
                                     pending_bytes_.begin() + data_offset_);
                return AudioDecodeResult::DataDecoded;
            }

            if (chunk_size > kMaxHeaderBytes || payload > pending_bytes_.size() ||
                chunk_size > pending_bytes_.size() - payload)
                return pending_bytes_.size() < kMaxHeaderBytes
                    ? AudioDecodeResult::NeedMoreData
                    : AudioDecodeResult::InvalidData;

            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                if (chunk_size < 16)
                    return AudioDecodeResult::InvalidData;
                wav_format_.audio_format = ReadU16LE(chunk + 8);
                wav_format_.channels = ReadU16LE(chunk + 10);
                wav_format_.sample_rate = ReadU32LE(chunk + 12);
                wav_format_.bits_per_sample = ReadU16LE(chunk + 22);
                if (wav_format_.audio_format != 1 || wav_format_.bits_per_sample != 16 ||
                    (wav_format_.channels != 1 && wav_format_.channels != 2) ||
                    wav_format_.sample_rate < 8000 || wav_format_.sample_rate > 192000 ||
                    ReadU32LE(chunk + 16) != wav_format_.sample_rate * wav_format_.channels * 2 ||
                    ReadU16LE(chunk + 20) != wav_format_.channels * 2)
                    return AudioDecodeResult::InvalidData;
                found_fmt = true;
            }
            const size_t padded_size = static_cast<size_t>(chunk_size) + (chunk_size & 1u);
            if (padded_size > pending_bytes_.size() - payload)
                return AudioDecodeResult::NeedMoreData;
            offset = payload + padded_size;
        }
    }

    AudioDecodeResult AudioStreamDecoder::DecodePCM()
    {
        const size_t bytes_per_frame = (wav_format_.bits_per_sample / 8) * wav_format_.channels;
        if (!data_size_unknown_ && pending_bytes_.size() > data_bytes_remaining_)
            return AudioDecodeResult::InvalidData;
        const size_t complete_bytes = (pending_bytes_.size() / bytes_per_frame) * bytes_per_frame;
        if (complete_bytes == 0)
            return AudioDecodeResult::NeedMoreData;

        const size_t input_channels = wav_format_.channels;
        for (size_t offset = 0; offset < complete_bytes; offset += bytes_per_frame)
        {
            for (size_t channel = 0; channel < input_channels; ++channel)
            {
                const uint16_t raw = ReadU16LE(pending_bytes_.data() + offset + channel * 2);
                const int16_t sample = static_cast<int16_t>(raw);
                resample_frames_.push_back(static_cast<float>(sample) / 32768.0f);
            }
        }
        pending_bytes_.erase(pending_bytes_.begin(), pending_bytes_.begin() + complete_bytes);
        if (!data_size_unknown_)
            data_bytes_remaining_ -= complete_bytes;

        const auto output_format = stream_->GetAudioFormat();
        if (output_format.sample_rate == 0 ||
            (output_format.channels != 1 && output_format.channels != 2))
            return AudioDecodeResult::InvalidData;

        const size_t available_frames = resample_frames_.size() / input_channels;
        std::vector<float> output;
        const double step = static_cast<double>(wav_format_.sample_rate) / output_format.sample_rate;
        while (resample_position_ + 1.0 < available_frames)
        {
            const size_t a = static_cast<size_t>(resample_position_);
            const size_t b = a + 1;
            const float t = static_cast<float>(resample_position_ - a);
            const float left = resample_frames_[a * input_channels] * (1.f - t) +
                               resample_frames_[b * input_channels] * t;
            const float right = input_channels == 1 ? left :
                resample_frames_[a * input_channels + 1] * (1.f - t) +
                resample_frames_[b * input_channels + 1] * t;
            if (output_format.channels == 1)
                output.push_back(input_channels == 1 ? left : (left + right) * 0.5f);
            else
            {
                output.push_back(left);
                output.push_back(right);
            }
            resample_position_ += step;
        }

        // Keep the frame immediately before the next interpolation position.
        const size_t discard_frames = std::min(static_cast<size_t>(resample_position_),
                                               available_frames > 0 ? available_frames - 1 : 0);
        if (discard_frames > 0)
        {
            resample_frames_.erase(resample_frames_.begin(),
                                   resample_frames_.begin() + discard_frames * input_channels);
            resample_position_ -= discard_frames;
        }

        const size_t output_frames = output.size() / output_format.channels;
        if (output_frames && !stream_->PushFrames(output.data(), output_frames))
            return AudioDecodeResult::InvalidData;
        output_frames_ += output_frames;
        return output_frames ? AudioDecodeResult::DataDecoded : AudioDecodeResult::NeedMoreData;
    }
}
