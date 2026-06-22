#ifndef KPENGINE_RUNTIME_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_PLAYER_H

#include <memory>
#include "audio_clip.h"

namespace kpengine::audio{
    class AudioPlayer{
    public:
    
    ~AudioPlayer() = default;
    void SetClip(std::shared_ptr<AudioClip> clip);
    void Play();
    void Stop();
    void Pause();

    bool IsPlaying() const { return playing;}

    std::shared_ptr<AudioClip> clip_;
        
        uint64_t current_frame_ = 0;
        bool looping = false;
        bool playing = false;
        float volume = 1.0;
    };
}

#endif