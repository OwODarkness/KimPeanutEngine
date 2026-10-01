#include "miniaudio_audio_system.h"
#include "log/logger.h"
#include "audio_player.h"
#include <miniaudio/miniaudio.h>

namespace kpengine::audio
{
    class MiniAudioSystem::MiniAudioWrapper{
    public:
        ma_device device;
    };

    static const char *LogName = "Miniaudio_AudioSystemLog";
    namespace
    {
        void DataCallback(
            ma_device *device,
            void *output,
            const void *input,
            ma_uint32 frameCount)
        {
            auto *system =
                static_cast<AudioSystem *>(device->pUserData);
            if (system == nullptr)
            {
                return;
            }
            system->Mix(
                static_cast<float *>(output),
                frameCount);
        }
    }

    MiniAudioSystem::MiniAudioSystem():
    wrapper_(std::make_unique<MiniAudioWrapper>())
    {
    }

    bool MiniAudioSystem::Initialize()
    {
        if (device_initialized_)
        {
            return true;
        }
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.periodSizeInFrames = 512;
        config.periods = 3;
        config.dataCallback = DataCallback;
        config.pUserData = this;


        if (ma_device_init(nullptr, &config, &(wrapper_->device)) != MA_SUCCESS)
        {
            return false;
        }
        device_initialized_ = true;
        if (ma_device_start(&(wrapper_->device)) != MA_SUCCESS)
        {
            ma_device_uninit(&(wrapper_->device));
            device_initialized_ = false;
            return false;
        }

        KP_LOG(LogName, LOG_LEVEL_INFO, "Audio Init succeed");
        return true;
    }
    void MiniAudioSystem::ShutDown()
    {
        if (!device_initialized_)
        {
            return;
        }
        // Stop waits for the callback to retire before system-owned players
        // or the callback target can be destroyed.
        ma_device_stop(&(wrapper_->device));
        ma_device_uninit(&(wrapper_->device));
        device_initialized_ = false;
    }

    void MiniAudioSystem::Mix(float *output, uint32_t frame_count)
    {
        constexpr uint32_t out_channels = 2;
        ClearOutputBuffer(output, frame_count * out_channels);

        const auto players = GetPlayerSnapshot();

        for (const auto &player_ptr : *players)
        {
            AudioPlayer *player = player_ptr.get();
            if (!player || !player->IsPlaying())
                continue;

            // Refill at most once per voice and callback block. A dry stream
            // outputs silence for the rest of this block and retries next time.
            player->FillBuffer();
            const float volume = player->GetVolume();
            uint64_t frame = player->GetCurrentFrame();

            for (uint32_t i = 0; i < frame_count && player->IsPlaying(); ++i)
            {
                uint64_t dst = i * out_channels;
                uint64_t left = dst;
                uint64_t right = dst + 1;

                float data[2]{};
                uint32_t in_channels = 0;
                if(!player->CopyFrameData(frame, data, 2, in_channels))
                {
                    if (player->IsFinished())
                        player->Stop();
                    break;
                }

                if (out_channels == 2 && in_channels == 1)
                {
                    output[left] += volume * data[0];
                    output[right] += volume * data[0];
                }
                else if (out_channels == 2 && in_channels >= 2)
                {
                    output[left] += volume *  data[0];
                    output[right] += volume *  data[1];
                }

                ++frame;
                if (player->AdvanceFrame() == false)
                {
                    // Only tear the player down when the source is actually
                    // finished; a temporary underrun must not stop playback.
                    if (player->IsFinished())
                    {
                        player->Stop();
                    }
                    break;
                }
            }
        }
    }

    void MiniAudioSystem::ClearOutputBuffer(float *output, uint32_t size)
    {
        memset(output, 0, size * sizeof(float));
    }

    MiniAudioSystem::~MiniAudioSystem()
    {
        ShutDown();
    }
}
