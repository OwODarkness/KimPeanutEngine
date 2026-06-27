#include "miniaudio_audio_system.h"
#include "log/logger.h"
#include "audio_player.h"

namespace kpengine::audio
{
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

    MiniAudioSystem::MiniAudioSystem()
    {
    }

    bool MiniAudioSystem::Initialize()
    {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.dataCallback = DataCallback;
        config.pUserData = this;

        if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS)
        {
            return false;
        }
        ma_device_start(&device_);
        KP_LOG("Audio", LOG_LEVEL_INFO,
               "Device sample rate: %u",
               device_.sampleRate);

        KP_LOG("AudioSysLog", LOG_LEVEL_INFO, "Audio Init succeed");
        return true;
    }
    void MiniAudioSystem::ShutDown()
    {
    }

    void MiniAudioSystem::Mix(float *output, uint32_t frame_count)
    {
        uint32_t out_channels = device_.playback.channels;
        if(out_channels != 2)
        {
            KP_LOG("MiniAudioSystemLog", LOG_LEVEL_WARNING, "channel %d mismatch, desired 2", out_channels);
            return ;
        }
        ClearOutputBuffer(output, frame_count * out_channels);

        for (auto &player_ptr : players_)
        {
            AudioPlayer *player = player_ptr.get();

            if (!player->IsPlaying() || !player->GetClip())
                continue;

            auto &clip = *player->GetClip();

            uint32_t in_channels = clip.channels;

            for (uint32_t i = 0; i < frame_count; i++)
            {
                if (player->IsFinished())
                {
                    player->Stop();
                    break;
                }

                uint64_t frame = player->GetCurrentFrame();
                uint64_t src = frame  * in_channels;
                uint64_t dst = i * out_channels;
                uint64_t left = dst;
                uint64_t right = dst + 1;

                float volume = player->GetVolume();

                if(out_channels == 1)
                {

                    float res = volume * clip.pcm[src];
                    output[left] = res;
                    output[right] = res;
                }
                else if(out_channels == 2)
                {
                    output[left] = volume * clip.pcm[src];
                    output[right] = volume * clip.pcm[src + 1];
                }

                player->SetCurrentFrame(frame+ 1);
            }
        }
    }

        void MiniAudioSystem::ClearOutputBuffer(float* output, uint32_t size)
        {
            memset(output, 0, size);
        }

    MiniAudioSystem::~MiniAudioSystem()
    {
    }
}