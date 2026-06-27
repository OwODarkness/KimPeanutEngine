#include "audio_player.h"

namespace kpengine::audio
{

    void  AudioPlayer::SetClip(std::shared_ptr<AudioClip> clip)
    {
        clip_ = clip;
    }

    void AudioPlayer::Play()
    {
        playing_ = true;
    }

    void AudioPlayer::Stop()
    {
        current_frame_ = 0;
        playing_ = false;
    }

    void AudioPlayer::Pause()
    {
        playing_ = false;
    }

    bool AudioPlayer::IsFinished() const
    {
        if(clip_ == nullptr)
            return true;
        return clip_->frame_count <= current_frame_;
    }

    void AudioPlayer::Reset()
    {
        clip_.reset();
        current_frame_ = 0;
        playing_ = false;
    }

    void AudioPlayer::SetVolume(float volume)
    {
        volume_ = volume;
    }

    void AudioPlayer::SetCurrentFrame(uint64_t new_frame)
    {
        if(clip_ == nullptr)
        {
            return ;
        }
        if(clip_->frame_count >= new_frame)
        {
            current_frame_ = new_frame;
        }
    }

} // namespace kpengine::audio
