#ifndef KPENGINE_RUNTIME_STREAM_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_STREAM_AUDIO_PLAYER_H

#include <memory>
#include <vector>
#include <mutex>
#include "audio_player.h"

namespace kpengine::audio
{
class AudioStream;

class StreamAudioPlayer : public AudioPlayer
{
public:
    StreamAudioPlayer();
    ~StreamAudioPlayer() override;

    const std::vector<float>& GetPCM() const override;
    bool GetFrameData(uint64_t src,
                      const float*& out_data) const override;

    AudioFormat GetAudioFormat() const override;

    void Play() override;

    bool FillBuffer() override;

    float GetCurrentSecond() const override;
    float GetRemainSecond() const override;
    bool SeekSeconds(float seconds) override;

    void SetStream(std::shared_ptr<AudioStream> stream);
    AudioStream* GetStream() const;

protected:
    bool ResolveFrame(uint64_t& new_frame) override;

private:
    // Pull more frames from the stream into the ring buffer so that the
    // window around `at_frame` stays above the low-water mark. Caller must
    // hold buffer_mutex_.
    void Refill(uint64_t at_frame);

    struct RingBuffer {
        std::vector<float> data;
        uint64_t start_frame = 0;      
        uint64_t filled_frames = 0;   
        uint64_t capacity_frames = 0;  
        
        uint64_t GetReadOffset(uint64_t frame_index) const {
            uint64_t local_frame = frame_index - start_frame;
            if (local_frame >= filled_frames) {
                return static_cast<uint64_t>(-1);
            }
            return local_frame;
        }
        
        float* GetWritePointer(uint64_t offset_frames) {
            return data.data() + offset_frames * channels;
        }
        
        int channels = 0;
    };
    
    std::shared_ptr<AudioStream> stream_;
    RingBuffer ring_buffer_;
    mutable std::mutex buffer_mutex_;
    
    // 配置参数
    static constexpr uint64_t CACHE_SIZE_FRAMES = 20240;  
    static constexpr uint64_t LOW_WATER_MARK = 5120;     
};

}

#endif