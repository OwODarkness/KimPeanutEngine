#include "stream_audio_player.h"
#include "audio_stream.h"
#include "log/logger.h"
#include <algorithm>
#include <cstring>

namespace kpengine::audio
{
    StreamAudioPlayer::StreamAudioPlayer()
    {
    }

    StreamAudioPlayer::~StreamAudioPlayer()
    {
    }

    bool StreamAudioPlayer::CopyFrameData(uint64_t frame_index, float* out_data,
                                          uint32_t capacity_samples, uint32_t& channels)
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;

        channels = static_cast<uint32_t>(ring_buffer_.channels);
        if (channels == 0 || channels > 2 || !out_data || capacity_samples < channels) {
            return false;
        }

        // `src` is a sample offset (frame * channels), matching the buffer
        // player convention. Translate it back to a frame index first.
        uint64_t offset = ring_buffer_.GetReadOffset(frame_index);
        if (offset == static_cast<uint64_t>(-1)) {
            if (stream_ && stream_->IsFinished() &&
                frame_index >= ring_buffer_.start_frame + ring_buffer_.filled_frames)
                state_.store(AudioState::Finished, std::memory_order_release);
            return false;
        }

        std::copy_n(ring_buffer_.data.data() + offset * channels, channels, out_data);
        return true;
    }

    AudioFormat StreamAudioPlayer::GetAudioFormat() const
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        if (!stream_) {
            return {};
        }
        return stream_->GetAudioFormat();
    }

    void StreamAudioPlayer::Play()
    {
        if (IsPlaying()) {
            return;
        }

        // Ring Buffer Init
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            if (!stream_) {
                KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_WARNING,
                       "player doesn't contain stream source");
                return;
            }
            auto format = stream_->GetAudioFormat();
            if (format.sample_rate != 48000 || format.channels == 0 || format.channels > 2)
                return;
            ring_buffer_.channels = format.channels;
            ring_buffer_.capacity_frames = CACHE_SIZE_FRAMES;
            ring_buffer_.data.assign(CACHE_SIZE_FRAMES * format.channels, 0.0f);
            ring_buffer_.start_frame = 0;
            ring_buffer_.filled_frames = 0;
        }

        // Prepare Init Chunk (best effort — a streaming source may not have
        // any data yet; Play() still starts and the Mix loop waits/refills).
        for (int i = 0; i < 3; ++i) {
            uint64_t dummy_frame = current_frame_;
            if (!ResolveFrame(dummy_frame)) {
                break;
            }
        }

        AudioPlayer::Play();

        KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_INFO,
               "Stream player started with ring buffer size: %llu frames",
               CACHE_SIZE_FRAMES);
    }

    void StreamAudioPlayer::Reset()
    {
        AudioPlayer::Reset();
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        stream_.reset();
        ring_buffer_.data.clear();
        ring_buffer_.channels = 0;
        ring_buffer_.capacity_frames = 0;
        ring_buffer_.start_frame = 0;
        ring_buffer_.filled_frames = 0;
    }

    void StreamAudioPlayer::SetStream(std::shared_ptr<AudioStream> stream)
    {
        if (!stream) {
            return;
        }
        const auto format = stream->GetAudioFormat();
        if (format.sample_rate != 48000 || format.channels == 0 || format.channels > 2)
            return;
        
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        stream_ = stream;
        ring_buffer_.start_frame = 0;
        ring_buffer_.filled_frames = 0;
        
        ring_buffer_.channels = format.channels;
        ring_buffer_.capacity_frames = CACHE_SIZE_FRAMES;
        ring_buffer_.data.assign(CACHE_SIZE_FRAMES * format.channels, 0.0f);
    }

    std::shared_ptr<AudioStream> StreamAudioPlayer::GetStream() const
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return stream_;
    }

    float StreamAudioPlayer::GetCurrentSecond() const
    {
        return static_cast<float>(current_frame_) / GetAudioFormat().sample_rate;
    }
    
    float StreamAudioPlayer::GetRemainSecond() const
    {
        return 0.f;
    }
    
    bool StreamAudioPlayer::SeekSeconds(float new_seconds)
    {
        const AudioFormat format = GetAudioFormat();
        if (!format.sample_rate) return false;
        uint64_t target_frame = static_cast<uint64_t>(
            new_seconds * format.sample_rate);
        
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            ring_buffer_.start_frame = target_frame;
            ring_buffer_.filled_frames = 0;
            current_frame_ = target_frame;
        }
        
        for (int i = 0; i < 3; ++i) {
            uint64_t dummy = current_frame_;
            if (!ResolveFrame(dummy)) break;
        }
        
        return true;
    }

    void StreamAudioPlayer::Refill(uint64_t at_frame)
    {
        // Caller must hold buffer_mutex_.
        if (!stream_ || ring_buffer_.capacity_frames == 0) {
            return;
        }

        // If the playhead is no longer inside the buffered window, restart
        // the window there so the next read lands exactly at the playhead.
        if (ring_buffer_.GetReadOffset(at_frame) == static_cast<uint64_t>(-1)) {
            ring_buffer_.start_frame = at_frame;
            ring_buffer_.filled_frames = 0;
        }

        uint64_t unplayed =
            (ring_buffer_.start_frame + ring_buffer_.filled_frames) - at_frame;
        if (unplayed > LOW_WATER_MARK) {
            return;
        }

        uint64_t max_write = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
        if (max_write == 0) {
            // Ring buffer full: slide the unplayed tail to the front.
            uint64_t frames_to_keep = unplayed;
            uint64_t frames_to_discard = ring_buffer_.filled_frames - frames_to_keep;

            float* src = ring_buffer_.data.data() + frames_to_discard * ring_buffer_.channels;
            float* dst = ring_buffer_.data.data();
            memmove(dst, src, frames_to_keep * ring_buffer_.channels * sizeof(float));

            ring_buffer_.start_frame += frames_to_discard;
            ring_buffer_.filled_frames = frames_to_keep;
            max_write = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
        }

        float* write_ptr = ring_buffer_.GetWritePointer(ring_buffer_.filled_frames);
        uint64_t read_frames = stream_->TryReadFrames(
            write_ptr,
            std::min(max_write, CACHE_SIZE_FRAMES / 4)
        );

        if (read_frames > 0) {
            ring_buffer_.filled_frames += read_frames;
        }
    }

    bool StreamAudioPlayer::FillBuffer()
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        Refill(current_frame_);
        if (stream_ && stream_->IsFinished() &&
            ring_buffer_.GetReadOffset(current_frame_) == static_cast<uint64_t>(-1) &&
            current_frame_ >= ring_buffer_.start_frame + ring_buffer_.filled_frames)
            state_.store(AudioState::Finished, std::memory_order_release);
        return stream_ &&
               ring_buffer_.GetReadOffset(current_frame_) != static_cast<uint64_t>(-1);
    }

    bool StreamAudioPlayer::AdvanceFrame()
    {
        current_frame_.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool StreamAudioPlayer::ResolveFrame(uint64_t& new_frame)
    {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            if (!stream_)
                return false;

            // Top the ring buffer up around the requested frame.
            Refill(new_frame);

            if (new_frame >= ring_buffer_.start_frame + ring_buffer_.filled_frames) {
                // The frame is not available yet. A temporary underrun is not
                // an error: leave the playhead here and let the caller retry
                // (outputting silence meanwhile). Only a finished source marks
                // the real end of playback.
                if (stream_->IsFinished()) {
                    state_ = AudioState::Finished;
                }
                return false;
            }
        }

        return true;
    }

}
