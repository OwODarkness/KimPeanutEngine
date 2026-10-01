#ifndef KPENGINE_RUNTIME_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_PLAYER_H

#include <memory>
#include <atomic>
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
        virtual bool CopyFrameData(uint64_t frame, float* out_data,
                                   uint32_t capacity_samples, uint32_t& channels) = 0;

        // Pull more source data into the player's internal buffer so a frame
        // that is not yet available can become available. No-op for players
        // that have their whole clip buffered up front.
        virtual bool FillBuffer() { return true; }

        bool IsPlaying() const { return state_.load(std::memory_order_acquire) == AudioState::Playing;}
        bool IsFinished() const {return state_.load(std::memory_order_acquire) == AudioState::Finished;}
        virtual AudioFormat GetAudioFormat() const = 0;
        AudioState GetCurrentState() const{return state_.load(std::memory_order_acquire);}
        uint64_t GetCurrentFrame() const { return current_frame_.load(std::memory_order_acquire); }

        virtual void SetVolume(float volume);
        virtual float GetVolume() const { return volume_.load(std::memory_order_relaxed); }

        virtual float GetCurrentSecond() const = 0;
        virtual float GetRemainSecond() const = 0;
        virtual bool SeekSeconds(float new_seconds) = 0;

        virtual void SetShouldLoop(bool looping);

        virtual bool AdvanceFrame();
        bool AdvanceSecond();
        bool SeekFrames(uint64_t new_frame);

    protected:
        bool SetCurrentFrame(uint64_t new_frame);

        virtual bool ResolveFrame(uint64_t& new_frame) = 0;
    protected:
        std::atomic<AudioState> state_{AudioState::Stopped};
        std::atomic<uint64_t> current_frame_{0};
        std::atomic<bool> looping_{false};
        std::atomic<float> volume_{1.0f};
    };
}

#endif
