#include "buffer_audio_player.h"

#include <algorithm>
#include <cmath>

namespace kpengine::audio
{
    BufferAudioPlayer::~BufferAudioPlayer() = default;

    uint32_t BufferAudioPlayer::CopyFrames(uint64_t first_frame, float* out_data,
                                           uint32_t max_frames, uint32_t& channels)
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        channels = clip ? clip->format.channels : 0;
        if (!clip || !out_data || clip->format.channels == 0 || clip->format.channels > 2 ||
            clip->frame_count == 0 || clip->pcm.size() / clip->format.channels < clip->frame_count)
            return 0;

        const uint64_t frame_count = clip->frame_count;
        const bool looping = looping_.load(std::memory_order_relaxed);
        if (!looping && first_frame >= frame_count)
            return 0;

        const uint32_t channel_count = clip->format.channels;
        uint32_t copied = 0;
        while (copied < max_frames)
        {
            const uint64_t source_frame = looping
                                              ? (first_frame + copied) % frame_count
                                              : first_frame + copied;
            if (source_frame >= frame_count)
                break;
            std::copy_n(clip->pcm.data() + source_frame * channel_count, channel_count,
                        out_data + static_cast<size_t>(copied) * channel_count);
            ++copied;
        }
        return copied;
    }

    void BufferAudioPlayer::SetClip(std::shared_ptr<const AudioClip> clip)
    {
        if (!clip)
            return;
        std::lock_guard lock(clip_mutex_);
        if (clip_owner_)
            return;
        clip_owner_ = std::move(clip);
        clip_.store(clip_owner_.get(), std::memory_order_release);
    }

    std::shared_ptr<const AudioClip> BufferAudioPlayer::GetClip() const
    {
        std::lock_guard lock(clip_mutex_);
        return clip_owner_;
    }

    AudioFormat BufferAudioPlayer::GetAudioFormat() const
    {
        const auto clip = GetClip();
        return clip ? clip->format : AudioFormat{};
    }

    bool BufferAudioPlayer::CanStartPlayback() const
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        return clip && clip->frame_count > 0 && clip->format.channels > 0 &&
               clip->format.channels <= 2 && clip->format.sample_rate == 48000 &&
               clip->frame_count <= clip->pcm.size() / clip->format.channels;
    }

    void BufferAudioPlayer::Play()
    {
        AudioPlayer::Play();
    }

    void BufferAudioPlayer::Reset()
    {
        AudioPlayer::Reset();
    }

    float BufferAudioPlayer::GetCurrentSecond() const
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        if (!clip || clip->frame_count == 0)
            return 0.f;
        return static_cast<float>(GetCurrentFrame()) / clip->frame_count * clip->GetDuration();
    }

    float BufferAudioPlayer::GetRemainSecond() const
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        if (!clip || clip->frame_count == 0)
            return 0.f;
        const float duration = clip->GetDuration();
        return duration - static_cast<float>(GetCurrentFrame()) / clip->frame_count * duration;
    }

    bool BufferAudioPlayer::ResolveFrame(uint64_t& new_frame)
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        if (!clip || clip->frame_count == 0)
            return false;
        if (new_frame < clip->frame_count)
            return true;
        if (looping_.load(std::memory_order_relaxed))
        {
            new_frame %= clip->frame_count;
            return true;
        }
        new_frame = clip->frame_count;
        MarkFinished();
        return true;
    }

    bool BufferAudioPlayer::IsSourceDrained() const
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        return clip && !looping_.load(std::memory_order_relaxed) &&
               GetCurrentFrame() >= clip->frame_count;
    }

    void BufferAudioPlayer::OnPlaybackCursorAdvanced(uint32_t frame_count)
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        if (!clip || clip->frame_count == 0)
            return;
        const uint64_t current = GetCurrentFrame();
        const uint64_t next = current + frame_count;
        if (looping_.load(std::memory_order_relaxed))
        {
            current_frame_.store(next % clip->frame_count, std::memory_order_release);
            return;
        }
        current_frame_.store(std::min(next, clip->frame_count), std::memory_order_release);
    }

    bool BufferAudioPlayer::SeekSeconds(float new_seconds)
    {
        const AudioClip* clip = clip_.load(std::memory_order_acquire);
        if (!clip || clip->GetDuration() <= 0.f || !std::isfinite(new_seconds) || new_seconds < 0.f)
            return false;
        const auto frame = static_cast<uint64_t>(clip->frame_count *
                                                 (new_seconds / clip->GetDuration()));
        return SeekFrames(frame);
    }
}
