#ifndef KPENGINE_RUNTIME_AUDIO_CLIP_H
#define KPENGINE_RUNTIME_AUDIO_CLIP_H

#include <vector>
#include <cstdint>

namespace kpengine::data{

    struct AudioClip{
        std::vector<float> pcm;
        uint32_t channels = 0;
        uint32_t  sample_rate = 0;
        uint64_t frame_count = 0;    
        
        float GetDuration() const
        {
            return sample_rate == 0 ? 0.f : static_cast<float>(frame_count) / sample_rate;
        }
    };
}

#endif