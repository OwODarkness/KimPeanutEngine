#include "stream_audio_player.h"
#include "audio_stream.h"
#include "log/logger.h"
#include <algorithm>

namespace kpengine::audio
{
    StreamAudioPlayer::StreamAudioPlayer()
    {
    }

    StreamAudioPlayer::~StreamAudioPlayer()
    {
    }

    const std::vector<float>& StreamAudioPlayer::GetPCM() const
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return ring_buffer_.data;
    }

    bool StreamAudioPlayer::GetFrameData(uint64_t src, const float*& out_data) const
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        
        uint64_t offset = ring_buffer_.GetReadOffset(src);
        if (offset == static_cast<uint64_t>(-1)) {
            return false;
        }
        
        out_data = ring_buffer_.data.data() + 
                   offset * GetAudioFormat().channels;
        return true;
    }

    AudioFormat StreamAudioPlayer::GetAudioFormat() const
    {
        if (!stream_) {
            return {};
        }
        return stream_->GetAudioFormat();
    }

    void StreamAudioPlayer::Play()
    {
        if (!stream_) {
            KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_WARNING,
                   "player doesn't contain stream source");
            return;
        }

        if (IsPlaying()) {
            return;
        }

        // Ring Buffer Init
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            auto format = stream_->GetAudioFormat();
            ring_buffer_.channels = format.channels;
            ring_buffer_.capacity_frames = CACHE_SIZE_FRAMES;
            ring_buffer_.data.assign(CACHE_SIZE_FRAMES * format.channels, 0.0f);
            ring_buffer_.start_frame = 0;
            ring_buffer_.filled_frames = 0;
        }

        // Prepare Init Chunk
        for (int i = 0; i < 3; ++i) {  
            uint64_t dummy_frame = 0;
            if (!ResolveFrame(dummy_frame)) {
                KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_WARNING,
                       "Failed to prefill buffer");
                return;
            }
        }

        AudioPlayer::Play();

        KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_INFO,
               "Stream player started with ring buffer size: %llu frames",
               CACHE_SIZE_FRAMES);
    }

    void StreamAudioPlayer::SetStream(std::shared_ptr<AudioStream> stream)
    {
        if (!stream) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        stream_ = stream;
        ring_buffer_.start_frame = 0;
        ring_buffer_.filled_frames = 0;
        
        auto format = stream->GetAudioFormat();
        ring_buffer_.channels = format.channels;
        ring_buffer_.capacity_frames = CACHE_SIZE_FRAMES;
        ring_buffer_.data.assign(CACHE_SIZE_FRAMES * format.channels, 0.0f);
    }

    AudioStream* StreamAudioPlayer::GetStream() const
    {
        return stream_.get();
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
        if (!stream_) return false;
        
        uint64_t target_frame = static_cast<uint64_t>(
            new_seconds * GetAudioFormat().sample_rate);
        
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

    bool StreamAudioPlayer::ResolveFrame(uint64_t& new_frame)
    {
        if (!stream_) return false;

        std::lock_guard<std::mutex> lock(buffer_mutex_);
        
        uint64_t current_offset = ring_buffer_.GetReadOffset(new_frame);
        
        if (current_offset != static_cast<uint64_t>(-1)) {
            if (ring_buffer_.filled_frames > LOW_WATER_MARK) {
                return true;
            }
            
            uint64_t write_start_frame = ring_buffer_.start_frame + ring_buffer_.filled_frames;
            uint64_t write_offset_frames = ring_buffer_.filled_frames;
            uint64_t max_write_frames = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
            
            if (max_write_frames == 0) {
                uint64_t frames_to_keep = ring_buffer_.filled_frames - LOW_WATER_MARK;
                uint64_t frames_to_discard = ring_buffer_.filled_frames - frames_to_keep;
                
                float* src = ring_buffer_.data.data() + frames_to_discard * ring_buffer_.channels;
                float* dst = ring_buffer_.data.data();
                memmove(dst, src, frames_to_keep * ring_buffer_.channels * sizeof(float));
                
                ring_buffer_.start_frame += frames_to_discard;
                ring_buffer_.filled_frames = frames_to_keep;
                write_start_frame = ring_buffer_.start_frame + ring_buffer_.filled_frames;
                write_offset_frames = ring_buffer_.filled_frames;
                max_write_frames = ring_buffer_.capacity_frames - ring_buffer_.filled_frames;
            }
            
            float* write_ptr = ring_buffer_.GetWritePointer(write_offset_frames);
            uint64_t read_frames = stream_->ReadFrames(
                write_ptr,
                std::min(max_write_frames, CACHE_SIZE_FRAMES / 4)  // 每次读一部分
            );
            
            if (read_frames > 0) {
                ring_buffer_.filled_frames += read_frames;
                
                KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_DEBUG,
                       "Buffer refilled: +%llu frames, total=%llu, start=%llu",
                       read_frames, ring_buffer_.filled_frames, ring_buffer_.start_frame);
            }
            
            if (read_frames == 0 && stream_->IsFinished()) {
                state_ = AudioState::Finished;
                return false;
            }
            
            return true;
        }
        
        ring_buffer_.start_frame = new_frame;
        ring_buffer_.filled_frames = 0;
        
        float* write_ptr = ring_buffer_.data.data();
        uint64_t read_frames = stream_->ReadFrames(
            write_ptr,
            std::min(CACHE_SIZE_FRAMES, CACHE_SIZE_FRAMES / 2)
        );
        
        if (read_frames > 0) {
            ring_buffer_.filled_frames = read_frames;
            
            if (read_frames < CACHE_SIZE_FRAMES) {
                std::fill(
                    ring_buffer_.data.begin() + read_frames * ring_buffer_.channels,
                    ring_buffer_.data.end(),
                    0.0f
                );
            }
            
            KP_LOG("LogStreamAudioPlayer", LOG_LEVEL_INFO,
                   "Buffer reset: read %llu frames at frame %llu",
                   read_frames, new_frame);
            
            return true;
        }
        
        return false;
    }

} 