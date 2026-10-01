#ifndef KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "audio_system.h"

namespace kpengine::audio
{
    enum class AudioDeviceSampleFormat : uint8_t
    {
        Unknown,
        Float32,
        Signed16,
        Signed24,
        Signed32,
        Unsigned8
    };

    struct AudioDeviceInfo
    {
        std::string name;
        AudioDeviceSampleFormat format = AudioDeviceSampleFormat::Unknown;
        uint32_t sample_rate = 0;
        uint32_t channels = 0;
        uint32_t period_size_frames = 0;
        uint32_t period_count = 0;
        uint32_t estimated_buffer_frames = 0;
        double estimated_latency_ms = 0.0;
        bool initialized = false;
    };

    class MiniAudioSystem : public AudioSystem
    {
    public:
        explicit MiniAudioSystem(AudioSystemSettings settings = {});
        bool Initialize() override;
        void ShutDown() override;
        bool IsInitialized() const override { return device_initialized_.load(std::memory_order_acquire); }
        AudioDeviceInfo GetDeviceInfo() const;
        ~MiniAudioSystem() override;

    private:
        class MiniAudioWrapper;
        std::unique_ptr<MiniAudioWrapper> wrapper_;
        std::atomic<bool> device_initialized_{false};
        mutable std::mutex device_mutex_;
        AudioDeviceInfo device_info_{};
    };
}

#endif
