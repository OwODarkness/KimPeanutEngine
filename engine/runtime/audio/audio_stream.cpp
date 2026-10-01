#include "audio_stream.h"
#include <limits>

namespace kpengine::audio
{
    AudioStream::AudioStream(const data::AudioFormat& format, uint32_t buffer_seconds)
        : format_(format), capacity_(0)
    {
        const size_t samples_per_second = static_cast<size_t>(format.sample_rate) * format.channels;
        if (samples_per_second > 0 && buffer_seconds <= std::numeric_limits<size_t>::max() / samples_per_second)
            capacity_ = samples_per_second * buffer_seconds;
        buffer_.resize(capacity_);
    }

bool AudioStream::PushFrames(const float* data, uint64_t frames)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t channels = format_.channels;
    if (!data || channels == 0 || capacity_ == 0 || is_finished.load(std::memory_order_acquire) ||
        frames > std::numeric_limits<size_t>::max() / channels)
    {
        return false;
    }
    const size_t samples = frames * channels;
    if (samples > AvailableSpace())
    {
        return false;
    }

    // Write first segment.
    size_t first = std::min(samples, capacity_ - write_pos_);

    std::copy(
        data,
        data + first,
        buffer_.begin() + write_pos_);

    // Write wrapped segment.
    size_t second = samples - first;

    if (second > 0)
    {
        std::copy(
            data + first,
            data + samples,
            buffer_.begin());
    }

    write_pos_ = (write_pos_ + samples) % capacity_;

    buffered_samples_ += samples;

    return true;
}

    size_t AudioStream::AvailableSpace() const
    {
        return capacity_ - buffered_samples_;
    }

uint64_t AudioStream::ReadFrames(float* output,
                                 uint64_t requested_frames)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ReadFramesLocked(output, requested_frames);
}

uint64_t AudioStream::TryReadFrames(float* output, uint64_t requested_frames)
{
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return 0;
    return ReadFramesLocked(output, requested_frames);
}

uint64_t AudioStream::ReadFramesLocked(float* output, uint64_t requested_frames)
{

    const size_t channels = format_.channels;
    if (!output || channels == 0 || capacity_ == 0)
        return 0;

    const size_t available_frames =
        buffered_samples_ / channels;

    const size_t frames_to_read =
        std::min<size_t>(
            requested_frames,
            available_frames);

    if (frames_to_read == 0)
        return 0;

    const size_t samples_to_read =
        frames_to_read * channels;

    size_t first =
        std::min(
            samples_to_read,
            capacity_ - read_pos_);

    std::copy(
        buffer_.begin() + read_pos_,
        buffer_.begin() + read_pos_ + first,
        output);

    size_t second =
        samples_to_read - first;

    if (second > 0)
    {
        std::copy(
            buffer_.begin(),
            buffer_.begin() + second,
            output + first);
    }

    read_pos_ =
        (read_pos_ + samples_to_read) % capacity_;

    buffered_samples_ -= samples_to_read;

    return frames_to_read;
}

    void AudioStream::Finish()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_finished.store(true, std::memory_order_release);
    }

    bool AudioStream::IsFinished() const
    {
        return is_finished.load(std::memory_order_acquire);
    }

}
