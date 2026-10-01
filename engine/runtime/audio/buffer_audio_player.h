#ifndef KPENGINE_RUNTIME_BUFFER_AUDIO_PLAYER_H
#define KPENGINE_RINTIME_BUFFER_AUIDO_PLAYER_H


#include "audio_player.h"

namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;

    class BufferAudioPlayer : public  AudioPlayer{
    public:
        ~BufferAudioPlayer();
        bool CopyFrameData(uint64_t frame, float* out_data,
                           uint32_t capacity_samples, uint32_t& channels) override;

        void SetClip(std::shared_ptr<AudioClip> clip);
        std::shared_ptr<AudioClip> GetClip() const;
        AudioFormat GetAudioFormat() const override;

        void Play() override;
        void Reset() override;

        float GetCurrentSecond() const override;
        float GetRemainSecond() const override;
        bool SeekSeconds(float new_seconds) override;

    protected:
        bool ResolveFrame(uint64_t& new_frame) override;

    private:
        std::atomic<std::shared_ptr<AudioClip>> clip_;
    }; 
} // namespace kpengine::audio


#endif
