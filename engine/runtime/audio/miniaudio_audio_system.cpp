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
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 48000;
        config.dataCallback = DataCallback;
        config.pUserData = this;


        if (ma_device_init(nullptr, &config, &(wrapper_->device)) != MA_SUCCESS)
        {
            return false;
        }
        ma_device_start(&(wrapper_->device));

        KP_LOG(LogName, LOG_LEVEL_INFO, "Audio Init succeed");
        return true;
    }
    void MiniAudioSystem::ShutDown()
    {
    }

    void MiniAudioSystem::Mix(float *output, uint32_t frame_count)
    {
        uint32_t out_channels = wrapper_->device.playback.channels;
        if (out_channels != 2)
        {
            KP_LOG(LogName, LOG_LEVEL_WARNING, "channel %d mismatch, desired 2", out_channels);
            return;
        }
        ClearOutputBuffer(output, frame_count * out_channels);

        for (uint32_t i = 0; i < frame_count; i++)
        {
            for (auto &player_ptr : players_)
            {
                AudioPlayer *player = player_ptr.get();

                if (!player->IsPlaying())
                    continue;



                AudioFormat audio_format = player->GetAudioFormat();

                uint32_t in_channels = audio_format.channels;

                uint64_t frame = player->GetCurrentFrame();
                uint64_t src = frame * in_channels;
                uint64_t dst = i * out_channels;
                uint64_t left = dst;
                uint64_t right = dst + 1;

                float volume = player->GetVolume();

                const float* data;
                if(!player->GetFrameData(src, data))
                {
                    // No playable frame yet (e.g. a streaming source waiting
                    // for more data): ask it to pull more, output silence.
                    player->FillBuffer();
                    continue;
                }

                if (out_channels == 1)
                {

                    float res = volume * data[0];
                    output[left] += res;
                    output[right] += res;
                }
                else if (out_channels == 2)
                {
                    output[left] += volume *  data[0];
                    output[right] += volume *  data[1];
                }

                if (player->AdvanceFrame() == false)
                {
                    // Only tear the player down when the source is actually
                    // finished; a temporary underrun must not stop playback.
                    if (player->IsFinished())
                    {
                        player->Stop();
                    }
                    continue;
                }
            }
        }
    }

    void MiniAudioSystem::ClearOutputBuffer(float *output, uint32_t size)
    {
        memset(output, 0, size);
    }

    MiniAudioSystem::~MiniAudioSystem()
    {
    }
}