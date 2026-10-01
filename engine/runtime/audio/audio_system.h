#ifndef KPENGINE_RUNTIME_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_AUDIO_SYSTEM_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "audio_types.h"

namespace kpengine::audio
{
    struct AudioSystemSettings
    {
        uint32_t max_voices = 64;
        uint32_t max_callback_frames = 512;
        uint32_t gain_ramp_frames = 240;
        uint32_t device_period_count = 3;
    };

    class AudioPlayer;
    class StreamAudioPlayer;

    struct AudioBusTelemetry
    {
        uint64_t played_frames = 0;
        uint64_t underrun_blocks = 0;
    };

    struct AudioTelemetrySnapshot
    {
        uint64_t callback_count = 0;
        uint64_t callback_frames = 0;
        uint64_t callback_work_limited_frames = 0;
        uint64_t max_callback_duration_ns = 0;
        uint64_t concurrent_callback_rejections = 0;
        uint64_t stream_overflow_rejections = 0;
        uint64_t control_command_rejections = 0;
        std::array<AudioBusTelemetry, static_cast<size_t>(AudioBus::Count)> buses{};
    };

    class AudioSystem
    {
    public:
        static constexpr uint32_t kMaxVoices = 64;
        static constexpr uint32_t kMaxCallbackFrames = 512;

        explicit AudioSystem(AudioSystemSettings settings = {});
        virtual ~AudioSystem();
        virtual bool Initialize() = 0;
        virtual void ShutDown() = 0;
        virtual bool IsInitialized() const { return false; }
        virtual void Mix(float* output, uint32_t frame_count);

        // Compatibility access for the current TTS producer. Playback control
        // calls on the player are mailbox commands consumed at the next Mix.
        std::shared_ptr<AudioPlayer> GetAudioPlayer(AudioHandle handle);
        AudioHandle CreateAudioPlayer(AudioPlayerType type);
        bool DestroyAudioPlayer(AudioHandle handle);

        void SetMasterGain(float gain);
        float GetMasterGain() const;
        void SetBusGain(AudioBus bus, float gain);
        float GetBusGain(AudioBus bus) const;
        void SetBusMuted(AudioBus bus, bool muted);
        bool IsBusMuted(AudioBus bus) const;
        AudioTelemetrySnapshot GetTelemetrySnapshot() const;
        const AudioSystemSettings& GetSettings() const { return settings_; }

    protected:
        struct VoiceSlot
        {
            std::shared_ptr<AudioPlayer> owner;
            AudioHandle handle{};
            std::atomic<AudioPlayer*> published{nullptr};
            std::atomic<uint32_t> callback_readers{0};
        };

        struct GainRamp
        {
            float current = 1.0f;
            float target = 1.0f;
            uint32_t remaining = 0;
            uint32_t duration_frames = 0;

            void Retarget(float new_target);
            float Next();
        };

        void WaitForCallbacks();

    private:
        static constexpr size_t kBusCount = static_cast<size_t>(AudioBus::Count);
        static size_t BusIndex(AudioBus bus);

        AudioSystemSettings settings_;
        HandleSystem<AudioHandle> handle_system_;
        std::array<VoiceSlot, kMaxVoices> voices_{};
        mutable std::mutex voices_mutex_;

        std::atomic<float> master_gain_target_{1.0f};
        std::array<std::atomic<float>, kBusCount> bus_gain_target_{};
        std::array<std::atomic<bool>, kBusCount> bus_muted_{};
        GainRamp master_gain_ramp_{};
        std::array<GainRamp, kBusCount> bus_gain_ramps_{};

        std::array<float, kMaxCallbackFrames * 2> voice_scratch_{};
        std::array<float, kMaxCallbackFrames> master_gain_scratch_{};
        std::array<std::array<float, kMaxCallbackFrames>, kBusCount> bus_gain_scratch_{};
        std::atomic_flag mix_guard_ = ATOMIC_FLAG_INIT;

        std::array<std::atomic<uint64_t>, kBusCount> bus_played_frames_{};
        std::array<std::atomic<uint64_t>, kBusCount> bus_underrun_blocks_{};
        std::atomic<uint64_t> callback_count_{0};
        std::atomic<uint64_t> callback_frames_{0};
        std::atomic<uint64_t> callback_work_limited_frames_{0};
        std::atomic<uint64_t> max_callback_duration_ns_{0};
        std::atomic<uint64_t> concurrent_callback_rejections_{0};
        std::atomic<uint64_t> retired_stream_overflow_rejections_{0};
        std::atomic<uint64_t> retired_control_command_rejections_{0};
    };
}

#endif
