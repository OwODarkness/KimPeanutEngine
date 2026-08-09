#ifndef KPENGINE_RUNTIME_BUFFER_AUDIO_PLAYER_H
#define KPENGINE_RINTIME_BUFFER_AUIDO_PLAYER_H


#include "audio_player.h"

namespace kpengine::audio
{
    using AudioClip = kpengine::data::AudioClip;

    class BufferAudioPlayer : public  AudioPlayer{
    public:
        ~BufferAudioPlayer();
        virtual const std::vector<float>& GetPCM() const override;
        virtual bool GetFrameData(uint64_t src, const float* & out_data) const override;

        void SetClip(std::shared_ptr<AudioClip> clip);
        AudioClip *GetClip() const;
        AudioFormat GetAudioFormat() const override;

        void Play() override;

        float GetCurrentSecond() const override;
        float GetRemainSecond() const override;
        bool SeekSeconds(float new_seconds) override;

    protected:
        bool ResolveFrame(uint64_t& new_frame) override;

    private:
        std::shared_ptr<AudioClip> clip_;
    }; 
} // namespace kpengine::audio


#endif