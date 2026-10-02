#ifndef KPENGINE_RUNTIME_BUFFER_AUDIO_PLAYER_H
#define KPENGINE_RINTIME_BUFFER_AUIDO_PLAYER_H


#include "audio_player.h"

#include <mutex>
#include <vector>

namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;

    class BufferAudioPlayer : public  AudioPlayer{
    public:
        ~BufferAudioPlayer();
        uint32_t CopyFrames(uint64_t first_frame, float* out_data,
                            uint32_t max_frames, uint32_t& channels) override;
        uint32_t CopyFramesAtRate(uint64_t first_frame, float* out_data,
                                  uint32_t max_frames, uint32_t& channels,
                                  float playback_rate) override;

        void SetClip(std::shared_ptr<const AudioClip> clip);
        std::shared_ptr<const AudioClip> GetClip() const;
        AudioFormat GetAudioFormat() const override;

        void Play() override;
        void Reset() override;

        float GetCurrentSecond() const override;
        float GetRemainSecond() const override;
        bool SeekSeconds(float new_seconds) override;
        bool SetPlaybackRate(float playback_rate);
        float GetPlaybackRate() const override
        {
            return playback_rate_.load(std::memory_order_acquire);
        }

    protected:
        bool ResolveFrame(uint64_t& new_frame) override;
        bool CanStartPlayback() const override;
        bool IsSourceDrained() const override;
        void OnPlaybackCursorAdvanced(uint32_t frame_count) override;
        void OnPlaybackCursorAdvanced(uint32_t frame_count, float playback_rate) override;
        void OnPlaybackCursorSet() override { fractional_frame_ = 0.0; }

    private:
        mutable std::mutex clip_mutex_;
        std::shared_ptr<const AudioClip> clip_owner_;
        std::atomic<const AudioClip*> clip_{nullptr};
        std::atomic<float> playback_rate_{1.0f};
        double fractional_frame_ = 0.0;
    }; 
} // namespace kpengine::audio


#endif
