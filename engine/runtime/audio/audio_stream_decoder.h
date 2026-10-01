#ifndef KPENGINE_RUNTIME_AUDIO_STREAM_DECODER_H
#define KPENGINE_RUNTIME_AUDIO_STREAM_DECODER_H

#include <cstdint>
#include <memory>
#include <vector>
#include "data/audio.h"
#include "audio_stream.h"

namespace kpengine::audio
{
    enum class AudioDecodeResult
    {
        NeedMoreData,
        DataDecoded,
        InvalidData
    };

    class AudioStreamDecoder
    {
    public:
        AudioStreamDecoder(std::shared_ptr<AudioStream> stream) : stream_(stream) {}

        ~AudioStreamDecoder() = default;

        AudioDecodeResult Feed(
            const uint8_t *data,
            size_t size);

        bool Finish();

    private:
        AudioDecodeResult ParseHeader();
        AudioDecodeResult DecodePCM();

    protected:
    protected:
        std::shared_ptr<AudioStream> stream_;
        std::vector<uint8_t> pending_bytes_;
        bool header_parsed_ = false;
        data::AudioFormat wav_format_;
        size_t data_offset_ = 0;
        uint64_t data_bytes_remaining_ = 0;
        bool data_size_unknown_ = false;
        std::vector<float> resample_frames_;
        double resample_position_ = 0.0;
        uint64_t output_frames_ = 0;
    };

}

#endif
