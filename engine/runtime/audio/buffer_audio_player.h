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

        void SetClip(std::shared_ptr<const AudioClip> clip);
        std::shared_ptr<const AudioClip> GetClip() const;
        AudioFormat GetAudioFormat() const override;

        void Play() override;
        void Reset() override;

        float GetCurrentSecond() const override;
        float GetRemainSecond() const override;
        bool SeekSeconds(float new_seconds) override;

    protected:
        bool ResolveFrame(uint64_t& new_frame) override;
        bool CanStartPlayback() const override;
        bool IsSourceDrained() const override;
        void OnPlaybackCursorAdvanced(uint32_t frame_count) override;

    private:
        mutable std::mutex clip_mutex_;
        std::shared_ptr<const AudioClip> clip_owner_;
        std::atomic<const AudioClip*> clip_{nullptr};
    }; 
} // namespace kpengine::audio


#endif
