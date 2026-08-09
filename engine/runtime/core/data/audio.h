#ifndef KPENGINE_RUNTIME_AUDIO_CLIP_H
#define KPENGINE_RUNTIME_AUDIO_CLIP_H

#include <vector>
#include <cstdint>

namespace kpengine::data
{
    struct AudioFormat
    {
        uint16_t audio_format;
        uint16_t channels = 1;
        uint32_t sample_rate = 48000;
        uint16_t bits_per_sample;
    };



    struct AudioClip
    {
    public:
        std::vector<float> pcm;
        AudioFormat format;
        uint64_t frame_count = 0;

        float GetDuration() const
        {
            return format.sample_rate == 0 ? 0.f : static_cast<float>(frame_count) / format.sample_rate;
        }
    };

    struct AudioChunk
    {
        std::vector<float> pcm;
        AudioFormat format;
        size_t frame_count = 0;
        bool end_of_stream = false;
        uint64_t timestamp = 0;
    };
}

#endif