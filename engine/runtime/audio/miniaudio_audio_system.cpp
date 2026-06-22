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
        config.playback.channels = 1;
        config.sampleRate = 32000;
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
    AudioPlayer *MiniAudioSystem::CreateAudioPlayer()
    {
        players_.emplace_back(std::make_unique<AudioPlayer>());
        return players_.back().get();
    }

void MiniAudioSystem::Mix(float* output, uint32_t frame_count)
{
    // mono = 1 channel ONLY
    memset(output, 0, sizeof(float) * frame_count);

    for (auto& player_ptr : players_)
    {
        AudioPlayer* player = player_ptr.get();

        if (!player->IsPlaying() || !player->clip_)
            continue;

        auto& clip = *player->clip_;

        uint32_t inChannels = clip.channels;

        for (uint32_t i = 0; i < frame_count; i++)
        {
            if (player->current_frame_ >= clip.frame_count)
            {
                player->playing = false;
                break;
            }

            uint64_t frame = player->current_frame_;
            uint64_t idx = frame * inChannels;

            float sample;
                sample = clip.pcm[idx];

            // mono / stereo safe downmix
            if (inChannels == 1)
            {
            }
            else
            {
                sample = 0.5f * (clip.pcm[idx + 0] + clip.pcm[idx + 1]);
            }

            output[i] += sample * player->volume;

            player->current_frame_++;
        }
    }
}
    MiniAudioSystem::~MiniAudioSystem()
    {
    }
}