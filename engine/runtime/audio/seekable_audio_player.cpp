#include "seekable_audio_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#include <miniaudio/miniaudio.h>

namespace kpengine::audio
{
    namespace
    {
        constexpr std::uint64_t kMaximumDurationFrames = 48000ull * 60ull * 60ull * 4ull;

        struct EncodedRangeReader
        {
            std::ifstream file;
            std::uint64_t range_offset{};
            std::uint64_t range_size{};
            std::uint64_t cursor{};
        };

        ma_result ReadEncodedRange(ma_decoder *decoder, void *output,
                                  const std::size_t requested, std::size_t *read)
        {
            if (read) *read = 0;
            if (!decoder || !decoder->pUserData || (!output && requested != 0))
                return MA_INVALID_ARGS;
            auto &source = *static_cast<EncodedRangeReader *>(decoder->pUserData);
            if (source.cursor >= source.range_size || requested == 0)
                return MA_SUCCESS;
            const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(
                requested, source.range_size - source.cursor));
            source.file.clear();
            source.file.seekg(static_cast<std::streamoff>(source.range_offset + source.cursor),
                              std::ios::beg);
            if (!source.file) return MA_ERROR;
            source.file.read(static_cast<char *>(output), static_cast<std::streamsize>(count));
            const std::streamsize actual = source.file.gcount();
            if (actual < 0) return MA_ERROR;
            source.cursor += static_cast<std::uint64_t>(actual);
            if (read) *read = static_cast<std::size_t>(actual);
            return source.file.bad() ? MA_ERROR : MA_SUCCESS;
        }

        ma_result SeekEncodedRange(ma_decoder *decoder, const ma_int64 offset,
                                   const ma_seek_origin origin)
        {
            if (!decoder || !decoder->pUserData) return MA_INVALID_ARGS;
            auto &source = *static_cast<EncodedRangeReader *>(decoder->pUserData);
            std::uint64_t base = 0;
            if (origin == ma_seek_origin_current) base = source.cursor;
            else if (origin == ma_seek_origin_end) base = source.range_size;
            else if (origin != ma_seek_origin_start) return MA_INVALID_ARGS;

            std::uint64_t target = 0;
            if (offset < 0)
            {
                const std::uint64_t magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
                if (magnitude > base) return MA_BAD_SEEK;
                target = base - magnitude;
            }
            else
            {
                const auto forward = static_cast<std::uint64_t>(offset);
                if (forward > source.range_size - std::min(base, source.range_size))
                    return MA_BAD_SEEK;
                target = base + forward;
            }
            if (target > source.range_size ||
                source.range_offset > static_cast<std::uint64_t>(
                    std::numeric_limits<std::streamoff>::max()) - target)
                return MA_BAD_SEEK;
            source.cursor = target;
            return MA_SUCCESS;
        }
    }

    SeekableAudioPlayer::SeekableAudioPlayer()
        : cache_(static_cast<std::size_t>(kCacheCapacityFrames) * kChannels, 0.0f)
    {
    }

    SeekableAudioPlayer::~SeekableAudioPlayer()
    {
        stop_worker_.store(true, std::memory_order_release);
        worker_condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    bool SeekableAudioPlayer::SetSource(FileBackedAudioSource source)
    {
        if (worker_.joinable() || source.path.empty() || source.encoded_size == 0 ||
            source.duration_frames == 0 || source.duration_frames > kMaximumDurationFrames ||
            !source.lifetime_pin ||
            source.encoded_offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max()) ||
            source.encoded_size > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max()) - source.encoded_offset)
        {
            SetDiagnostic("file-backed audio source descriptor is invalid");
            return false;
        }

        source_ = std::move(source);
        duration_frames_.store(source_.duration_frames, std::memory_order_release);
        try
        {
            worker_ = std::thread(&SeekableAudioPlayer::DecodeWorker, this);
        }
        catch (const std::system_error &error)
        {
            SetDiagnostic(std::string{"failed to start audio decode worker: "} + error.what());
            return false;
        }

        std::unique_lock lock(worker_mutex_);
        const bool started = worker_condition_.wait_for(
            lock, std::chrono::seconds{15}, [this] { return startup_complete_; });
        if (!started || !startup_success_)
        {
            lock.unlock();
            stop_worker_.store(true, std::memory_order_release);
            worker_condition_.notify_all();
            if (worker_.joinable()) worker_.join();
            if (!started) SetDiagnostic("audio decode worker startup timed out");
            return false;
        }
        return true;
    }

    std::string SeekableAudioPlayer::GetDiagnostic() const
    {
        std::lock_guard lock(diagnostic_mutex_);
        return diagnostic_;
    }

    std::uint64_t SeekableAudioPlayer::GetBufferedFrameCount() const
    {
        std::lock_guard lock(cache_mutex_);
        return cache_filled_frames_;
    }

    std::size_t SeekableAudioPlayer::GetFixedBufferBytes() const noexcept
    {
        return (static_cast<std::size_t>(kCacheCapacityFrames) + kDecodeChunkFrames) *
               kChannels * sizeof(float);
    }

    std::uint32_t SeekableAudioPlayer::CopyBufferedFrames(
        const std::uint64_t first_frame, float *out_data,
        const std::uint32_t max_frames, std::uint32_t &channels) const noexcept
    {
        channels = kChannels;
        if (out_data == nullptr || max_frames == 0)
            return 0;
        std::unique_lock lock(cache_mutex_, std::try_to_lock);
        if (!lock.owns_lock() || first_frame < cache_start_frame_)
            return 0;
        const std::uint64_t offset = first_frame - cache_start_frame_;
        if (offset >= cache_filled_frames_)
            return 0;
        const auto frame_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            max_frames, cache_filled_frames_ - offset));
        const std::size_t first_sample = static_cast<std::size_t>(offset) * kChannels;
        const std::size_t sample_count = static_cast<std::size_t>(frame_count) * kChannels;
        std::copy_n(cache_.data() + first_sample, sample_count, out_data);
        return frame_count;
    }

    std::uint32_t SeekableAudioPlayer::CopyFrames(
        const std::uint64_t first_frame, float *out_data,
        const std::uint32_t max_frames, std::uint32_t &channels)
    {
        return CopyFramesAtRate(first_frame, out_data, max_frames, channels, 1.0f);
    }

    std::uint32_t SeekableAudioPlayer::CopyFramesAtRate(
        const std::uint64_t first_frame, float *out_data, const std::uint32_t max_frames,
        std::uint32_t &channels, const float playback_rate)
    {
        channels = kChannels;
        if (!out_data || max_frames == 0 || !std::isfinite(playback_rate) ||
            playback_rate < 0.5f || playback_rate > 2.0f)
            return 0;
        std::unique_lock lock(cache_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return 0;

        const std::uint64_t duration = duration_frames_.load(std::memory_order_acquire);
        if (duration == 0) return 0;
        const bool looping = looping_.load(std::memory_order_acquire);
        double source_position = static_cast<double>(first_frame) + fractional_frame_;
        std::uint32_t copied = 0;
        for (; copied < max_frames; ++copied)
        {
            if (looping)
                source_position = std::fmod(source_position, static_cast<double>(duration));
            else if (source_position >= static_cast<double>(duration))
                break;

            const auto source_frame = static_cast<std::uint64_t>(source_position);
            auto next_frame = source_frame + 1 < duration
                ? source_frame + 1
                : (looping ? 0 : source_frame);
            if (source_frame < cache_start_frame_ ||
                source_frame - cache_start_frame_ >= cache_filled_frames_)
                break;
            if (next_frame != source_frame &&
                (next_frame < cache_start_frame_ ||
                 next_frame - cache_start_frame_ >= cache_filled_frames_))
            {
                if (looping && source_frame + 1 == duration)
                    next_frame = source_frame;
                else
                    break;
            }

            const std::size_t first_sample = static_cast<std::size_t>(
                source_frame - cache_start_frame_) * kChannels;
            const std::size_t second_sample = static_cast<std::size_t>(
                next_frame - cache_start_frame_) * kChannels;
            const float interpolation = static_cast<float>(source_position - source_frame);
            for (std::uint32_t channel = 0; channel < kChannels; ++channel)
            {
                const float first = cache_[first_sample + channel];
                const float second = cache_[second_sample + channel];
                out_data[static_cast<std::size_t>(copied) * kChannels + channel] =
                    first + (second - first) * interpolation;
            }
            source_position += playback_rate;
        }
        return copied;
    }

    AudioFormat SeekableAudioPlayer::GetAudioFormat() const
    {
        AudioFormat format{};
        format.channels = kChannels;
        format.sample_rate = kSampleRate;
        format.bits_per_sample = 32;
        return format;
    }

    float SeekableAudioPlayer::GetCurrentSecond() const
    {
        return static_cast<float>(GetCurrentFrame()) / static_cast<float>(kSampleRate);
    }

    float SeekableAudioPlayer::GetRemainSecond() const
    {
        const std::uint64_t duration = duration_frames_.load(std::memory_order_acquire);
        const std::uint64_t frame = GetCurrentFrame();
        return static_cast<float>(duration > frame ? duration - frame : 0) /
               static_cast<float>(kSampleRate);
    }

    bool SeekableAudioPlayer::SeekSeconds(const float seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0.0f) return false;
        const double frame = static_cast<double>(seconds) * kSampleRate;
        if (frame > static_cast<double>(duration_frames_.load(std::memory_order_acquire)))
            return false;
        return SeekFrames(static_cast<std::uint64_t>(frame));
    }

    bool SeekableAudioPlayer::SetPlaybackRate(const float playback_rate)
    {
        if (!std::isfinite(playback_rate) || playback_rate < 0.5f || playback_rate > 2.0f)
            return false;
        playback_rate_.store(playback_rate, std::memory_order_release);
        return true;
    }

    float SeekableAudioPlayer::GetPlaybackRate() const
    {
        return playback_rate_.load(std::memory_order_acquire);
    }

    bool SeekableAudioPlayer::FillBuffer()
    {
        if (decode_failed_.load(std::memory_order_acquire))
        {
            MarkCancelled();
            return false;
        }
        worker_condition_.notify_one();
        std::unique_lock lock(cache_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        const std::uint64_t frame = GetCurrentFrame();
        const bool cached = frame >= cache_start_frame_ &&
            frame - cache_start_frame_ < cache_filled_frames_;
        if (cached)
        {
            MarkPlaying();
            return true;
        }
        if (eof_.load(std::memory_order_acquire) &&
            frame >= duration_frames_.load(std::memory_order_acquire) &&
            !looping_.load(std::memory_order_acquire))
        {
            MarkFinished();
            return false;
        }
        MarkBuffering();
        return false;
    }

    bool SeekableAudioPlayer::ResolveFrame(std::uint64_t &new_frame)
    {
        const std::uint64_t duration = duration_frames_.load(std::memory_order_acquire);
        if (duration == 0) return false;
        if (new_frame >= duration)
        {
            if (looping_.load(std::memory_order_acquire))
                new_frame %= duration;
            else
            {
                new_frame = duration;
                MarkFinished();
            }
        }
        return true;
    }

    bool SeekableAudioPlayer::CanStartPlayback() const
    {
        return ready_.load(std::memory_order_acquire) &&
               !decode_failed_.load(std::memory_order_acquire);
    }

    bool SeekableAudioPlayer::IsSourceDrained() const
    {
        return eof_.load(std::memory_order_acquire) &&
               GetCurrentFrame() >= duration_frames_.load(std::memory_order_acquire) &&
               !looping_.load(std::memory_order_acquire);
    }

    void SeekableAudioPlayer::OnPlaybackCursorAdvanced(const std::uint32_t frame_count)
    {
        OnPlaybackCursorAdvanced(frame_count, GetPlaybackRate());
    }

    void SeekableAudioPlayer::OnPlaybackCursorAdvanced(
        const std::uint32_t frame_count, const float playback_rate)
    {
        const std::uint64_t duration = duration_frames_.load(std::memory_order_acquire);
        if (duration == 0 || frame_count == 0) return;
        const double advanced = fractional_frame_ +
            static_cast<double>(frame_count) * playback_rate;
        const std::uint64_t whole_frames = static_cast<std::uint64_t>(advanced);
        fractional_frame_ = advanced - static_cast<double>(whole_frames);
        const std::uint64_t next = GetCurrentFrame() + whole_frames;
        if (looping_.load(std::memory_order_acquire))
        {
            if (next >= duration)
                loop_serial_.fetch_add(1, std::memory_order_acq_rel);
            current_frame_.store(next % duration, std::memory_order_release);
        }
        else
        {
            current_frame_.store(std::min(next, duration), std::memory_order_release);
        }
        worker_condition_.notify_one();
    }

    void SeekableAudioPlayer::OnPlaybackCursorSet()
    {
        fractional_frame_ = 0.0;
        seek_target_.store(GetCurrentFrame(), std::memory_order_release);
        seek_serial_.fetch_add(1, std::memory_order_acq_rel);
        eof_.store(false, std::memory_order_release);
        worker_condition_.notify_one();
    }

    void SeekableAudioPlayer::SetDiagnostic(std::string diagnostic)
    {
        std::lock_guard lock(diagnostic_mutex_);
        diagnostic_ = std::move(diagnostic);
    }

    bool SeekableAudioPlayer::IsStopping() const noexcept
    {
        return stop_worker_.load(std::memory_order_acquire);
    }

    void SeekableAudioPlayer::DecodeWorker()
    {
        EncodedRangeReader reader{};
        reader.file.open(source_.path, std::ios::binary);
        reader.range_offset = source_.encoded_offset;
        reader.range_size = source_.encoded_size;
        const auto fail_startup = [this](std::string message)
        {
            SetDiagnostic(std::move(message));
            decode_failed_.store(true, std::memory_order_release);
            std::lock_guard lock(worker_mutex_);
            if (!startup_complete_)
            {
                startup_complete_ = true;
                startup_success_ = false;
            }
            worker_condition_.notify_all();
        };
        if (!reader.file.is_open())
        {
            fail_startup("failed to open file-backed audio product");
            return;
        }
        std::error_code file_error;
        const std::uint64_t file_size = std::filesystem::file_size(source_.path, file_error);
        if (file_error || source_.encoded_offset > file_size ||
            source_.encoded_size > file_size - source_.encoded_offset)
        {
            fail_startup("encoded audio range is outside the product file");
            return;
        }

        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
        ma_decoder decoder{};
        const ma_result init_result = ma_decoder_init(
            &ReadEncodedRange, &SeekEncodedRange, &reader, &config, &decoder);
        if (init_result != MA_SUCCESS)
        {
            fail_startup(std::string{"failed to initialize audio decoder: "} +
                         ma_result_description(init_result));
            return;
        }
        struct DecoderCleanup
        {
            ma_decoder &decoder;
            ~DecoderCleanup() { ma_decoder_uninit(&decoder); }
        } cleanup{decoder};

        ma_uint64 decoder_frames = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &decoder_frames) != MA_SUCCESS ||
            decoder_frames != duration_frames_.load(std::memory_order_acquire))
        {
            fail_startup("decoded duration does not match native Audio metadata");
            return;
        }

        std::vector<float> decode_buffer(
            static_cast<std::size_t>(kDecodeChunkFrames) * kChannels, 0.0f);
        std::uint64_t decoded_position = 0;
        std::uint64_t handled_seek = seek_serial_.load(std::memory_order_acquire);
        std::uint64_t handled_loop = loop_serial_.load(std::memory_order_acquire);
        bool at_end = false;
        while (!IsStopping())
        {
            const std::uint64_t seek = seek_serial_.load(std::memory_order_acquire);
            if (seek != handled_seek)
            {
                const std::uint64_t target = seek_target_.load(std::memory_order_acquire);
                if (ma_decoder_seek_to_pcm_frame(&decoder, target) != MA_SUCCESS)
                {
                    fail_startup("audio decoder rejected a seek request");
                    return;
                }
                decoded_position = target;
                handled_seek = seek;
                at_end = target >= duration_frames_.load(std::memory_order_acquire);
                eof_.store(at_end, std::memory_order_release);
                std::lock_guard lock(cache_mutex_);
                cache_start_frame_ = target;
                cache_filled_frames_ = 0;
            }

            const std::uint64_t loop = loop_serial_.load(std::memory_order_acquire);
            if (at_end && looping_.load(std::memory_order_acquire) && loop != handled_loop)
            {
                if (ma_decoder_seek_to_pcm_frame(&decoder, 0) != MA_SUCCESS)
                {
                    fail_startup("audio decoder rejected the loop seek");
                    return;
                }
                decoded_position = 0;
                handled_loop = loop;
                at_end = false;
                eof_.store(false, std::memory_order_release);
                std::lock_guard lock(cache_mutex_);
                cache_start_frame_ = 0;
                cache_filled_frames_ = 0;
            }

            if (at_end)
            {
                std::unique_lock lock(worker_mutex_);
                worker_condition_.wait_for(lock, std::chrono::milliseconds{5}, [this, handled_seek, handled_loop]
                {
                    return IsStopping() || seek_serial_.load(std::memory_order_acquire) != handled_seek ||
                           loop_serial_.load(std::memory_order_acquire) != handled_loop;
                });
                continue;
            }

            std::uint64_t writable = 0;
            {
                std::lock_guard lock(cache_mutex_);
                const std::uint64_t playback_frame = GetCurrentFrame();
                if (playback_frame > cache_start_frame_)
                {
                    const std::uint64_t discard = std::min(
                        playback_frame - cache_start_frame_, cache_filled_frames_);
                    if (discard != 0)
                    {
                        const std::size_t remaining_samples = static_cast<std::size_t>(
                            cache_filled_frames_ - discard) * kChannels;
                        std::memmove(cache_.data(), cache_.data() + discard * kChannels,
                                     remaining_samples * sizeof(float));
                        cache_start_frame_ += discard;
                        cache_filled_frames_ -= discard;
                    }
                }
                writable = kCacheCapacityFrames - cache_filled_frames_;
            }
            if (writable == 0)
            {
                std::unique_lock lock(worker_mutex_);
                worker_condition_.wait_for(lock, std::chrono::milliseconds{5}, [this, handled_seek]
                {
                    return IsStopping() || seek_serial_.load(std::memory_order_acquire) != handled_seek;
                });
                continue;
            }

            const std::uint64_t remaining =
                duration_frames_.load(std::memory_order_acquire) - decoded_position;
            if (remaining == 0)
            {
                at_end = true;
                eof_.store(true, std::memory_order_release);
                continue;
            }
            const ma_uint64 request_frames = static_cast<ma_uint64>(std::min<std::uint64_t>(
                {writable, kDecodeChunkFrames, remaining}));
            ma_uint64 frames_read = 0;
            const ma_result read_result = ma_decoder_read_pcm_frames(
                &decoder, decode_buffer.data(), request_frames, &frames_read);
            if (read_result != MA_SUCCESS && read_result != MA_AT_END)
            {
                fail_startup(std::string{"audio decode worker failed: "} +
                             ma_result_description(read_result));
                return;
            }

            if (frames_read != 0)
            {
                if (seek_serial_.load(std::memory_order_acquire) == handled_seek)
                {
                    std::lock_guard lock(cache_mutex_);
                    const std::uint64_t cache_end = cache_start_frame_ + cache_filled_frames_;
                    if (decoded_position == cache_end &&
                        frames_read <= kCacheCapacityFrames - cache_filled_frames_)
                    {
                        const std::size_t destination =
                            static_cast<std::size_t>(cache_filled_frames_) * kChannels;
                        const std::size_t sample_count =
                            static_cast<std::size_t>(frames_read) * kChannels;
                        std::copy_n(decode_buffer.data(), sample_count, cache_.data() + destination);
                        cache_filled_frames_ += frames_read;
                    }
                }
                decoded_position += frames_read;
                ready_.store(true, std::memory_order_release);
                {
                    std::lock_guard lock(worker_mutex_);
                    if (!startup_complete_)
                    {
                        startup_complete_ = true;
                        startup_success_ = true;
                    }
                }
                worker_condition_.notify_all();
            }

            if (read_result == MA_AT_END || frames_read == 0 || decoded_position >=
                duration_frames_.load(std::memory_order_acquire))
            {
                at_end = true;
                eof_.store(true, std::memory_order_release);
            }
        }
    }
}
