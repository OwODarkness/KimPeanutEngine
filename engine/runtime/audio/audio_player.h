#ifndef KPENGINE_RUNTIME_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_PLAYER_H

#include <memory>
#include "data/audio.h"
#include "audio_types.h"
namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;
    using AudioFormat = kpengine::data::AudioFormat;


    class AudioPlayer
    {
    public:
        virtual ~AudioPlayer() = default;
        virtual void Play();
        virtual void Stop();
        virtual void Pause();
        virtual void Reset();
        virtual void Restart();
        virtual bool GetFrameData(uint64_t src, const float* & out_data) const = 0;
        virtual const std::vector<float>& GetPCM() const = 0;

        // Pull more source data into the player's internal buffer so a frame
        // that is not yet available can become available. No-op for players
        // that have their whole clip buffered up front.
        virtual bool FillBuffer() { return true; }

        bool IsPlaying() const { return state_ == AudioState::Playing;}
        bool IsFinished() const {return state_ == AudioState::Finished;}
        virtual AudioFormat GetAudioFormat() const = 0;
        AudioState GetCurrentState() const{return state_;}
        uint64_t GetCurrentFrame() const { return current_frame_; }

        virtual void SetVolume(float volume);
        virtual float GetVolume() const { return volume_; }

        virtual float GetCurrentSecond() const = 0;
        virtual float GetRemainSecond() const = 0;
        virtual bool SeekSeconds(float new_seconds) = 0;

        virtual void SetShouldLoop(bool looping);

        bool AdvanceFrame();
        bool AdvanceSecond();
        bool SeekFrames(uint64_t new_frame);

    protected:
        bool SetCurrentFrame(uint64_t new_frame);

        virtual bool ResolveFrame(uint64_t& new_frame) = 0;
    protected:
        AudioState state_ = AudioState::Stopped;
        uint64_t current_frame_ = 0;
        bool looping_ = false;
        float volume_ = 1.0;
    };
}

#endif