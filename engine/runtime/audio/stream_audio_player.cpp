#include "stream_audio_player.h"

#include "audio_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kpengine::audio
{
    StreamAudioPlayer::StreamAudioPlayer() = default;
    StreamAudioPlayer::~StreamAudioPlayer() = default;

    uint32_t StreamAudioPlayer::CopyFrames(uint64_t first_frame, float* out_data,
                                            uint32_t max_frames, uint32_t& channels)
    {
        std::unique_lock lock(buffer_mutex_, std::try_to_lock);
        channels = lock.owns_lock() ? static_cast<uint32_t>(ring_buffer_.channels) : 0;
        if (!lock.owns_lock() || !out_data || ring_buffer_.channels == 0)
            return 0;

        const uint64_t offset = ring_buffer_.GetReadOffset(first_frame);
        if (offset == static_cast<uint64_t>(-1))
            return 0;

        const uint64_t available = ring_buffer_.filled_frames - offset;
        const uint32_t copied = static_cast<uint32_t>(
            std::min<uint64_t>(available, max_frames));
        const size_t sample_count = static_cast<size_t>(copied) * ring_buffer_.channels;
        std::copy_n(ring_buffer_.data.data() + offset * ring_buffer_.channels,
                    sample_count, out_data);
        return copied;
    }

    AudioFormat StreamAudioPlayer::GetAudioFormat() const
    {
        std::lock_guard lock(buffer_mutex_);
        return stream_ ? stream_->GetAudioFormat() : AudioFormat{};
    }

    bool StreamAudioPlayer::CanStartPlayback() const
    {
        return stream_ready_.load(std::memory_order_acquire);
    }

    void StreamAudioPlayer::Play()
    {
        AudioPlayer::Play();
    }

    void StreamAudioPlayer::Reset()
    {
        AudioPlayer::Reset();
    }

    void StreamAudioPlayer::SetStream(std::shared_ptr<AudioStream> stream)
    {
        if (!stream || IsActive() || stream_raw_.load(std::memory_order_acquire) != nullptr)
            return;
        const auto format = stream->GetAudioFormat();
        if (format.sample_rate != 48000 || format.channels == 0 || format.channels > 2)
            return;

        std::lock_guard lock(buffer_mutex_);
        if (stream_)
            return;
        stream_ = std::move(stream);
        ring_buffer_.channels = format.channels;
        ring_buffer_.capacity_frames = CACHE_SIZE_FRAMES;
        ring_buffer_.data.assign(CACHE_SIZE_FRAMES * format.channels, 0.0f);
        ring_buffer_.start_frame = 0;
        ring_buffer_.filled_frames = 0;
        buffered_end_frame_.store(0, std::memory_order_release);
        stream_raw_.store(stream_.get(), std::memory_order_release);
        stream_ready_.store(true, std::memory_order_release);
    }

    std::shared_ptr<AudioStream> StreamAudioPlayer::GetStream() const
    {
        std::lock_guard lock(buffer_mutex_);
        return stream_;
    }

    float StreamAudioPlayer::GetCurrentSecond() const
    {
        const AudioFormat format = GetAudioFormat();
        return format.sample_rate == 0 ? 0.0f
                                       : static_cast<float>(GetCurrentFrame()) / format.sample_rate;
    }

    float StreamAudioPlayer::GetRemainSecond() const
    {
        return 0.f;
    }

    bool StreamAudioPlayer::SeekSeconds(float seconds)
    {
        const AudioFormat format = GetAudioFormat();
        if (format.sample_rate == 0 || !std::isfinite(seconds) || seconds < 0.0f)
            return false;
        return SeekFrames(static_cast<uint64_t>(seconds * format.sample_rate));
    }

    void StreamAudioPlayer::Refill(uint64_t at_frame)
    {
        if (!stream_ || ring_buffer_.capacity_frames == 0)
            return;

        const uint64_t window_end = ring_buffer_.start_frame + ring_buffer_.filled_frames;
        if (at_frame < ring_buffer_.start_frame || at_frame > window_end)
        {
            ring_buffer_.start_frame = at_frame;
            ring_buffer_.filled_frames = 0;
        }

        const uint64_t unplayed =
            ring_buffer_.start_frame + ring_buffer_.filled_frames - at_frame;
        if (unplayed > LOW_WATER_MARK)
            return;

        uint64_t max_write = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
        if (max_write == 0)
        {
            const uint64_t frames_to_keep = unplayed;
            const uint64_t frames_to_discard = ring_buffer_.filled_frames - frames_to_keep;
            float* source = ring_buffer_.data.data() + frames_to_discard * ring_buffer_.channels;
            float* destination = ring_buffer_.data.data();
            std::memmove(destination, source,
                         frames_to_keep * ring_buffer_.channels * sizeof(float));
            ring_buffer_.start_frame += frames_to_discard;
            ring_buffer_.filled_frames = frames_to_keep;
            max_write = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
        }

        const uint64_t read_frames = stream_->TryReadFrames(
            ring_buffer_.GetWritePointer(ring_buffer_.filled_frames),
            std::min(max_write, CACHE_SIZE_FRAMES / 4));
        ring_buffer_.filled_frames += read_frames;
        buffered_end_frame_.store(ring_buffer_.start_frame + ring_buffer_.filled_frames,
                                  std::memory_order_release);
    }

    bool StreamAudioPlayer::FillBuffer()
    {
        std::unique_lock lock(buffer_mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !stream_)
            return false;

        const AudioStreamState stream_state = stream_->GetState();
        if (stream_state == AudioStreamState::Cancelled)
        {
            ring_buffer_.filled_frames = 0;
            buffered_end_frame_.store(ring_buffer_.start_frame, std::memory_order_release);
            MarkCancelled();
            return false;
        }

        Refill(GetCurrentFrame());
        const uint64_t offset = ring_buffer_.GetReadOffset(GetCurrentFrame());
        if (offset != static_cast<uint64_t>(-1))
        {
            MarkPlaying();
            return true;
        }

        const uint64_t end_frame = ring_buffer_.start_frame + ring_buffer_.filled_frames;
        if (stream_->GetState() == AudioStreamState::Drained && GetCurrentFrame() >= end_frame)
        {
            MarkFinished();
            return false;
        }

        MarkBuffering();
        return false;
    }

    bool StreamAudioPlayer::ResolveFrame(uint64_t& new_frame)
    {
        std::unique_lock lock(buffer_mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !stream_)
            return false;
        const uint64_t offset = ring_buffer_.GetReadOffset(new_frame);
        return offset != static_cast<uint64_t>(-1);
    }

    bool StreamAudioPlayer::IsSourceDrained() const
    {
        const AudioStream* stream = stream_raw_.load(std::memory_order_acquire);
        return stream && stream->GetState() == AudioStreamState::Drained &&
               GetCurrentFrame() >= buffered_end_frame_.load(std::memory_order_acquire);
    }

    void StreamAudioPlayer::OnPlaybackCursorAdvanced(uint32_t frame_count)
    {
        current_frame_.fetch_add(frame_count, std::memory_order_acq_rel);
    }
}
