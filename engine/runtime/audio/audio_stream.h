#ifndef KPENGINE_RUNTIME_AUDIO_STREAM_H
#define KPENGINE_RUNTIME_AUDIO_STREAM_H

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>
#include "data/audio.h"

namespace kpengine::audio
{

    class AudioStream
    {
    public:
        // `buffer_seconds` sizes the FIFO capacity in seconds of audio. It is
        // a caller's trade-off: large enough to absorb a producer that bursts
        // faster than real-time playback, small enough to bound memory. The
        // default matches the tuned streaming-TTS behavior.
        AudioStream(const data::AudioFormat& format, uint32_t buffer_seconds = 20);
    
        // Reject writes that do not fit; speech is never silently truncated.
        bool PushFrames(const float *data, uint64_t frames);

        uint64_t  ReadFrames(float *output, uint64_t frames);
        // Non-blocking consumer path used by the device callback.
        uint64_t TryReadFrames(float* output, uint64_t frames);

        void Finish();

        bool IsFinished() const;

        data::AudioFormat GetAudioFormat() const  {return format_;}

    private:
        size_t AvailableSpace() const;
        uint64_t ReadFramesLocked(float* output, uint64_t frames);
    private:
        std::atomic<bool> is_finished{false};
        data::AudioFormat format_;
        std::vector<float> buffer_;
        size_t write_pos_ = 0;
        size_t read_pos_ = 0;
        size_t capacity_;
        size_t buffered_samples_ = 0;
        mutable std::mutex mutex_;
    };
} // namespace  kpengine::audio

#endif
