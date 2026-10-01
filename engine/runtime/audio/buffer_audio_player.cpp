#include "buffer_audio_player.h"
#include "log/logger.h"

#include <algorithm>

namespace kpengine::audio
{
    namespace
    {
        constexpr const char* LogName = "LogBufferAudioPlayer";
    }

    BufferAudioPlayer::~BufferAudioPlayer() = default;

    bool BufferAudioPlayer::CopyFrameData(uint64_t frame, float* out_data,
                                          uint32_t capacity_samples, uint32_t& channels)
    {
        const auto clip = GetClip();
        channels = clip ? clip->format.channels : 0;
        if (!clip || !out_data || channels == 0 || channels > 2 ||
            clip->frame_count == 0 || capacity_samples < channels || frame >= clip->frame_count ||
            frame > clip->pcm.size() / channels || channels > clip->pcm.size() - frame * channels)
            return false;
        std::copy_n(clip->pcm.data() + frame * channels, channels, out_data);
        return true;
    }

    void BufferAudioPlayer::SetClip(std::shared_ptr<AudioClip> clip)
    {
        clip_.store(std::move(clip), std::memory_order_release);
    }

    std::shared_ptr<AudioClip> BufferAudioPlayer::GetClip() const
    {
        return clip_.load(std::memory_order_acquire);
    }

    AudioFormat BufferAudioPlayer::GetAudioFormat() const
    {
        const auto clip = GetClip();
        return clip ? clip->format : AudioFormat{};
    }

    void BufferAudioPlayer::Play()
    {
        const auto clip = GetClip();
        if (!clip || clip->frame_count == 0 || clip->format.channels == 0 ||
            clip->format.channels > 2 || clip->format.sample_rate != 48000 ||
            clip->frame_count > clip->pcm.size() / clip->format.channels)
        {
            KP_LOG(LogName, LOG_LEVEL_WARNING, "Failed to play invalid or empty audio");
            return;
        }
        AudioPlayer::Play();
    }

    void BufferAudioPlayer::Reset()
    {
        AudioPlayer::Reset();
        clip_.store(nullptr, std::memory_order_release);
    }

    float BufferAudioPlayer::GetCurrentSecond() const
    {
        const auto clip = GetClip();
        if (!clip || clip->frame_count == 0)
            return 0.f;
        return static_cast<float>(current_frame_) / clip->frame_count * clip->GetDuration();
    }

    float BufferAudioPlayer::GetRemainSecond() const
    {
        const auto clip = GetClip();
        if (!clip || clip->frame_count == 0)
            return 0.f;
        const float duration = clip->GetDuration();
        return duration - static_cast<float>(current_frame_) / clip->frame_count * duration;
    }

    bool BufferAudioPlayer::ResolveFrame(uint64_t& new_frame)
    {
        const auto clip = GetClip();
        if (!clip)
            return false;
        if (new_frame < clip->frame_count)
            return true;
        if (looping_ && clip->frame_count > 0)
        {
            new_frame %= clip->frame_count;
            return true;
        }
        if (clip->frame_count == 0)
        {
            new_frame = 0;
            state_ = AudioState::Finished;
            return false;
        }
        new_frame = clip->frame_count - 1;
        state_ = AudioState::Finished;
        return true;
    }

    bool BufferAudioPlayer::SeekSeconds(float new_seconds)
    {
        const auto clip = GetClip();
        if (!clip || clip->GetDuration() <= 0.f || new_seconds < 0.f)
            return false;
        const float progress = new_seconds / clip->GetDuration();
        return SetCurrentFrame(static_cast<uint64_t>(clip->frame_count * progress));
    }
}
