#ifndef KPENGINE_RUNTIME_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_PLAYER_H

#include <memory>
#include <atomic>
#include <array>
#include <mutex>
#include <cstddef>
#include "data/audio.h"
#include "audio_types.h"
namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;
    using AudioFormat = kpengine::data::AudioFormat;


    class AudioPlayer
    {
    public:
        AudioPlayer();
        virtual ~AudioPlayer() = default;
        virtual void Play();
        virtual void Stop();
        virtual void Pause();
        virtual void Reset();
        virtual void Restart();
        virtual uint32_t CopyFrames(uint64_t first_frame, float* out_data,
                                    uint32_t max_frames, uint32_t& channels) = 0;
        bool CopyFrameData(uint64_t frame, float* out_data,
                           uint32_t capacity_samples, uint32_t& channels);

        // Pull more source data into the player's internal buffer so a frame
        // that is not yet available can become available. No-op for players
        // that have their whole clip buffered up front.
        virtual bool FillBuffer() { return true; }

        bool IsPlaying() const;
        bool IsActive() const;
        bool IsFinished() const {return state_.load(std::memory_order_acquire) == AudioState::Finished;}
        bool IsCancelled() const { return state_.load(std::memory_order_acquire) == AudioState::Cancelled; }
        bool IsStreamingSourcePublic() const { return IsStreamingSource(); }
        virtual AudioFormat GetAudioFormat() const = 0;
        AudioState GetCurrentState() const{return state_.load(std::memory_order_acquire);}
        uint64_t GetCurrentFrame() const { return current_frame_.load(std::memory_order_acquire); }
        uint64_t GetPlayedFrameCursor() const { return GetCurrentFrame(); }

        virtual void SetVolume(float volume);
        virtual float GetVolume() const { return volume_.load(std::memory_order_relaxed); }
        void SetBus(AudioBus bus) { bus_.store(bus, std::memory_order_release); }
        AudioBus GetBus() const { return bus_.load(std::memory_order_acquire); }

        virtual float GetCurrentSecond() const = 0;
        virtual float GetRemainSecond() const = 0;
        virtual bool SeekSeconds(float new_seconds) = 0;

        virtual void SetShouldLoop(bool looping);

        virtual bool AdvanceFrame();
        bool AdvanceSecond();
        bool SeekFrames(uint64_t new_frame);

        // Called by the mixer exactly at a device callback boundary.
        void ApplyPendingCommand(uint32_t gain_ramp_frames);
        void BeginMixCallback(uint32_t gain_ramp_frames);
        void CommitPlayedFrames(uint32_t frame_count);
        void MarkBuffering();
        void MarkPlaying();
        float NextVoiceGain();
        void RefreshSourceState();
        uint64_t GetControlCommandRejectionCount() const
        {
            return control_command_rejections_.load(std::memory_order_relaxed);
        }

    protected:
        bool SetCurrentFrame(uint64_t new_frame);
        virtual bool CanStartPlayback() const { return true; }
        virtual bool IsSourceDrained() const { return false; }
        virtual bool IsStreamingSource() const { return false; }
        virtual void OnPlaybackCursorAdvanced(uint32_t frame_count) = 0;
        void MarkFinished();
        void MarkCancelled();

        virtual bool ResolveFrame(uint64_t& new_frame) = 0;
    protected:
        std::atomic<AudioState> state_{AudioState::Stopped};
        std::atomic<uint64_t> current_frame_{0};
        std::atomic<bool> looping_{false};
        std::atomic<bool> pending_looping_{false};
        std::atomic<float> volume_{1.0f};
        std::atomic<AudioBus> bus_{AudioBus::Speech};

    private:
        enum class Command : uint8_t { Play, Pause, Stop, Reset, Restart, Seek, SetLoop };
        static constexpr size_t kCommandQueueCapacity = 16;
        struct CommandSlot
        {
            std::atomic<size_t> sequence{0};
            Command command = Command::Play;
            uint64_t value = 0;
        };
        bool QueueCommand(Command command, uint64_t value = 0);
        bool DequeueCommand(Command& command, uint64_t& value);
        void ApplyCommand(Command command, uint64_t value);
        void BeginGainRamp(float target);
        void FinishFadeIfNeeded();

        std::array<CommandSlot, kCommandQueueCapacity> command_queue_{};
        std::atomic<size_t> command_write_position_{0};
        size_t command_read_position_ = 0;
        std::atomic<uint64_t> control_command_rejections_{0};
        float voice_gain_current_ = 1.0f;
        float voice_gain_target_ = 1.0f;
        uint32_t voice_gain_ramp_remaining_ = 0;
        uint32_t gain_ramp_duration_frames_ = 0;
        AudioState fade_completion_state_ = AudioState::Stopped;
    };
}

#endif
