#ifndef KPENGINE_RUNTIME_AUDIO_STREAM_DECODER_H
#define KPENGINE_RUNTIME_AUDIO_STREAM_DECODER_H

#include <cstdint>
#include <memory>
#include <vector>
#include "data/audio.h"
#include "audio_stream.h"

namespace kpengine::audio
{

    class AudioStreamDecoder
    {
    public:
        AudioStreamDecoder(std::shared_ptr<AudioStream> stream) : stream_(stream) {}

        ~AudioStreamDecoder() = default;

        bool Feed(
            const uint8_t *data,
            size_t size);

        void Finish();

    private:
        bool ParseHeader();
        bool DecodePCM();

    protected:
    protected:
        std::shared_ptr<AudioStream> stream_;
        std::vector<uint8_t> pending_bytes_;
        bool header_parsed_ = false;
        data::AudioFormat wav_format_;
        size_t data_offset_;
        size_t sample_total_ = 0;
    };

}

#endif