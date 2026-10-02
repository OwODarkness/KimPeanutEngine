#ifndef KPENGINE_RUNTIME_AUDIO_SEEKABLE_AUDIO_PLAYER_H
#define KPENGINE_RUNTIME_AUDIO_SEEKABLE_AUDIO_PLAYER_H

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_player.h"

namespace kpengine::audio
{
    struct FileBackedAudioSource
    {
        std::filesystem::path path;
        std::uint64_t encoded_offset{};
        std::uint64_t encoded_size{};
        std::uint64_t duration_frames{};
        std::shared_ptr<const void> lifetime_pin;
    };

    class SeekableAudioPlayer final : public AudioPlayer
    {
    public:
        static constexpr std::uint32_t kCacheCapacityFrames = 48000 * 2;
        static constexpr std::uint32_t kDecodeChunkFrames = 1024;

        SeekableAudioPlayer();
        ~SeekableAudioPlayer() override;
        SeekableAudioPlayer(const SeekableAudioPlayer &) = delete;
        SeekableAudioPlayer &operator=(const SeekableAudioPlayer &) = delete;

        bool SetSource(FileBackedAudioSource source);
        std::string GetDiagnostic() const;
        std::uint64_t GetBufferedFrameCount() const;
        std::size_t GetFixedBufferBytes() const noexcept;
        std::uint32_t CopyBufferedFrames(std::uint64_t first_frame, float *out_data,
                                         std::uint32_t max_frames,
                                         std::uint32_t &channels) const noexcept;

        std::uint32_t CopyFrames(std::uint64_t first_frame, float *out_data,
                                 std::uint32_t max_frames, std::uint32_t &channels) override;
        std::uint32_t CopyFramesAtRate(std::uint64_t first_frame, float *out_data,
                                       std::uint32_t max_frames, std::uint32_t &channels,
                                       float playback_rate) override;
        AudioFormat GetAudioFormat() const override;
        float GetCurrentSecond() const override;
        float GetRemainSecond() const override;
        bool SeekSeconds(float seconds) override;
        bool SetPlaybackRate(float playback_rate);
        float GetPlaybackRate() const override;
        bool FillBuffer() override;

    protected:
        bool ResolveFrame(std::uint64_t &new_frame) override;
        bool CanStartPlayback() const override;
        bool IsSourceDrained() const override;
        bool IsStreamingSource() const override { return true; }
        void OnPlaybackCursorAdvanced(std::uint32_t frame_count) override;
        void OnPlaybackCursorAdvanced(std::uint32_t frame_count,
                                      float playback_rate) override;
        void OnPlaybackCursorSet() override;

    private:
        void DecodeWorker();
        void SetDiagnostic(std::string diagnostic);
        bool IsStopping() const noexcept;

        static constexpr std::uint32_t kChannels = 2;
        static constexpr std::uint32_t kSampleRate = 48000;

        FileBackedAudioSource source_;
        std::vector<float> cache_;
        mutable std::mutex cache_mutex_;
        std::uint64_t cache_start_frame_{};
        std::uint64_t cache_filled_frames_{};
        std::atomic<std::uint64_t> duration_frames_{0};
        std::atomic<float> playback_rate_{1.0f};
        double fractional_frame_{};

        std::thread worker_;
        std::atomic<bool> stop_worker_{false};
        std::atomic<bool> ready_{false};
        std::atomic<bool> eof_{false};
        std::atomic<bool> decode_failed_{false};
        std::atomic<std::uint64_t> seek_serial_{0};
        std::atomic<std::uint64_t> seek_target_{0};
        std::atomic<std::uint64_t> loop_serial_{0};
        mutable std::mutex worker_mutex_;
        std::condition_variable worker_condition_;
        bool startup_complete_{};
        bool startup_success_{};
        mutable std::mutex diagnostic_mutex_;
        std::string diagnostic_;
    };
}

#endif
