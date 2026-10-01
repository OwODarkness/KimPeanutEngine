#include "miniaudio_audio_system.h"

#include "log/logger.h"
#include <miniaudio/miniaudio.h>

#include <algorithm>
#include <limits>

namespace kpengine::audio
{
    class MiniAudioSystem::MiniAudioWrapper
    {
    public:
        ma_device device{};
    };

    namespace
    {
        constexpr const char* kLogName = "Miniaudio_AudioSystemLog";

        void DataCallback(ma_device* device, void* output, const void*, ma_uint32 frame_count)
        {
            auto* system = static_cast<AudioSystem*>(device->pUserData);
            if (system)
                system->Mix(static_cast<float*>(output), frame_count);
        }

        AudioDeviceSampleFormat ToAudioDeviceSampleFormat(ma_format format)
        {
            switch (format)
            {
            case ma_format_f32: return AudioDeviceSampleFormat::Float32;
            case ma_format_s16: return AudioDeviceSampleFormat::Signed16;
            case ma_format_s24: return AudioDeviceSampleFormat::Signed24;
            case ma_format_s32: return AudioDeviceSampleFormat::Signed32;
            case ma_format_u8: return AudioDeviceSampleFormat::Unsigned8;
            default: return AudioDeviceSampleFormat::Unknown;
            }
        }
    }

    MiniAudioSystem::~MiniAudioSystem()
    {
        ShutDown();
    }

    MiniAudioSystem::MiniAudioSystem(AudioSystemSettings settings)
        : AudioSystem(settings)
        , wrapper_(std::make_unique<MiniAudioWrapper>())
    {
    }

    bool MiniAudioSystem::Initialize()
    {
        std::lock_guard lock(device_mutex_);
        if (device_initialized_.load(std::memory_order_acquire))
            return true;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.periodSizeInFrames = GetSettings().max_callback_frames;
        config.periods = GetSettings().device_period_count;
        config.dataCallback = DataCallback;
        config.pUserData = this;

        if (ma_device_init(nullptr, &config, &wrapper_->device) != MA_SUCCESS)
            return false;
        if (ma_device_start(&wrapper_->device) != MA_SUCCESS)
        {
            ma_device_uninit(&wrapper_->device);
            return false;
        }

        const auto& playback = wrapper_->device.playback;
        device_info_.name = playback.name;
        device_info_.format = ToAudioDeviceSampleFormat(playback.internalFormat);
        device_info_.sample_rate = playback.internalSampleRate;
        device_info_.channels = playback.internalChannels;
        device_info_.period_size_frames = playback.internalPeriodSizeInFrames;
        device_info_.period_count = playback.internalPeriods;
        const uint64_t estimated_buffer_frames =
            static_cast<uint64_t>(device_info_.period_size_frames) * device_info_.period_count;
        device_info_.estimated_buffer_frames = static_cast<uint32_t>(std::min<uint64_t>(
            estimated_buffer_frames, std::numeric_limits<uint32_t>::max()));
        device_info_.estimated_latency_ms = device_info_.sample_rate == 0
                                                ? 0.0
                                                : 1000.0 * device_info_.estimated_buffer_frames /
                                                      device_info_.sample_rate;
        device_info_.initialized = true;
        device_initialized_.store(true, std::memory_order_release);
        KP_LOG(kLogName, LOG_LEVEL_INFO,
               "Audio initialized: %s, %u Hz, %u channels, period %u x %u frames (estimated %.2f ms)",
               device_info_.name.c_str(), device_info_.sample_rate, device_info_.channels,
               device_info_.period_size_frames, device_info_.period_count,
               device_info_.estimated_latency_ms);
        return true;
    }

    void MiniAudioSystem::ShutDown()
    {
        std::lock_guard lock(device_mutex_);
        if (!device_initialized_.load(std::memory_order_acquire))
            return;

        ma_device_stop(&wrapper_->device);
        ma_device_uninit(&wrapper_->device);
        WaitForCallbacks();
        device_info_.initialized = false;
        device_initialized_.store(false, std::memory_order_release);
    }

    AudioDeviceInfo MiniAudioSystem::GetDeviceInfo() const
    {
        std::lock_guard lock(device_mutex_);
        return device_info_;
    }
}
