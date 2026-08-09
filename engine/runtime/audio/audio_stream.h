#ifndef KPENGINE_RUNTIME_AUDIO_STREAM_H
#define KPENGINE_RUNTIME_AUDIO_STREAM_H

#include <cstdint>
#include <mutex>
#include <vector>
#include "data/audio.h"

namespace kpengine::audio
{

    class AudioStream
    {
    public:
        AudioStream(const data::AudioFormat& format);
    
        void PushFrames(const float *data, uint64_t frames);

        uint64_t  ReadFrames(float *output, uint64_t frames);

        void Finish();

        bool IsFinished() const;

        data::AudioFormat GetAudioFormat() const  {return format_;}

    private:
        size_t AvailableSpace() const;
        size_t AvailableFrames() const ;
    private:
        bool is_finished = false;
        data::AudioFormat format_;
        std::vector<float> buffer_;
        size_t write_pos_ = 0;
        size_t read_pos_ = 0;
        size_t capacity_;
        size_t buffered_samples_ = 0;
        std::mutex mutex_;
    };
} // namespace  kpengine::audio

#endif