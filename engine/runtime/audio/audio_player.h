#ifndef KPENGINE_RUNTIME_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_PLAYER_H

#include <memory>
#include "data/audioclip.h"

namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;


    class AudioPlayer
    {
    public:
        ~AudioPlayer() = default;
        void SetClip(std::shared_ptr<AudioClip> clip);
        AudioClip* GetClip() const{return clip_.get();}
        void Play();
        void Stop();
        void Pause();
        void Reset();
        
        bool IsPlaying() const { return playing_; }
        bool IsFinished() const;

        void SetVolume(float volume);
        float GetVolume() const{return volume_;}

        uint64_t GetCurrentFrame() const{return current_frame_;}
        void SetCurrentFrame(uint64_t new_frame);

    private:
        std::shared_ptr<AudioClip> clip_;
        uint64_t current_frame_ = 0;
        bool looping_ = false;
        float volume_ = 1.0;
        bool playing_ = false;

    };
}

#endif