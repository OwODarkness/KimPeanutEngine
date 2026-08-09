#include "audio_stream.h"
#include "log/logger.h"

namespace kpengine::audio
{
    constexpr uint32_t BUFFER_SECONDS = 10;

    AudioStream::AudioStream(const data::AudioFormat& format)
        : format_(format)
    {
        // 1 second of samples.
        capacity_ = format.sample_rate * format.channels * BUFFER_SECONDS;

        buffer_.resize(capacity_);
    }

void AudioStream::PushFrames(const float* data, uint64_t frames)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t channels = format_.channels;
    const size_t samples = frames * channels;

    // If incoming data is larger than the whole buffer,
    // only keep the newest part.
    if (samples >= capacity_)
    {
        data += samples - capacity_;
        write_pos_ = 0;
        read_pos_ = 0;
        buffered_samples_ = 0;

        size_t first = capacity_;
        std::copy(data, data + first, buffer_.begin());

        write_pos_ = 0;
        buffered_samples_ = capacity_;

        KP_LOG("AudioStream",
               LOG_LEVEL_WARNING,
               "Incoming packet larger than ring buffer, old data discarded.");

        return;
    }

    // Make room if necessary by discarding oldest samples.
    if (samples > AvailableSpace())
    {
        size_t overflow = samples - AvailableSpace();

        read_pos_ = (read_pos_ + overflow) % capacity_;
        buffered_samples_ -= overflow;
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

    KP_LOG("AudioStream",
           LOG_LEVEL_DEBUG,
           "Push %llu frames, buffered=%zu/%zu samples",
           frames,
           buffered_samples_,
           capacity_);
}

    size_t AudioStream::AvailableSpace() const
    {
        return capacity_ - buffered_samples_;
    }

    size_t AudioStream::AvailableFrames() const
    {
        return buffered_samples_ / format_.channels;
    }

uint64_t AudioStream::ReadFrames(float* output,
                                 uint64_t requested_frames)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t channels = format_.channels;

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

    KP_LOG("AudioStream",
           LOG_LEVEL_DEBUG,
           "Read %zu frames, buffered=%zu/%zu samples",
           frames_to_read,
           buffered_samples_,
           capacity_);

    return frames_to_read;
}

    void AudioStream::Finish()
    {
        is_finished = true;
    }

    bool AudioStream::IsFinished() const
    {
        return is_finished;
    }

}