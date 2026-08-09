#include "buffer_audio_player.h"
#include "log/logger.h"
namespace kpengine::audio
{
    static const char *LogName = "LogBufferAudioPlayer";
    BufferAudioPlayer::~BufferAudioPlayer()
    {
    }

    const std::vector<float> &BufferAudioPlayer::GetPCM() const
    {
        if (!clip_)
        {
            throw std::runtime_error("GetPCM() called with null clip");
        }
        return clip_->pcm;
    }

    bool BufferAudioPlayer::GetFrameData(uint64_t src, const float* & out_data) const
    {
        if(!clip_ || clip_->pcm.size() <= src)
        {
            return false;
        }

        out_data = clip_->pcm.data() + src;
        return true;
    }

    void BufferAudioPlayer::SetClip(std::shared_ptr<AudioClip> clip)
    {
        clip_ = clip;
    }

    AudioClip *BufferAudioPlayer::GetClip() const
    {
        return clip_.get();
    }

    AudioFormat BufferAudioPlayer::GetAudioFormat() const
    {
        if (!clip_)
        {
            return {};
        }
        return clip_->format;
    }

    void BufferAudioPlayer::Play()
    {
        if (clip_ == nullptr)
        {
            KP_LOG(LogName, LOG_LEVEL_WARNING, "Failed to play empty audio");
            return;
        }
        KP_LOG(LogName, LOG_LEVEL_INFO, "ready to play audio, during : %.1lf s, current : %.1f s", clip_->GetDuration(), GetCurrentSecond());
        AudioPlayer::Play();
    }

    float BufferAudioPlayer::GetCurrentSecond() const
    {
        if (!clip_ || clip_->frame_count == 0)
            return 0.f;
        return (float)current_frame_ / clip_->frame_count * clip_->GetDuration();
    }
    float BufferAudioPlayer::GetRemainSecond() const
    {
        if (!clip_ || clip_->frame_count == 0)
            return 0.f;
        float duration = clip_->GetDuration();
        return duration - (float)current_frame_ / clip_->frame_count * duration;
    }

    bool BufferAudioPlayer::ResolveFrame(uint64_t &new_frame)
    {
        if (!clip_)
        {
            return false;
        }

        if (new_frame < clip_->frame_count)
        {
            return true;
        }

        if (looping_)
        {
            new_frame = new_frame % clip_->frame_count;
            return true;
        }

        new_frame = clip_->frame_count - 1;
        state_ = AudioState::Finished;
        return true;
    }

    bool BufferAudioPlayer::SeekSeconds(float new_seconds)
    {
        if (!clip_)
            return false;
        float progress = new_seconds / clip_->GetDuration();
        uint64_t frame_count = clip_->frame_count;
        return SetCurrentFrame(static_cast<uint64_t>(frame_count * progress));
    }
}