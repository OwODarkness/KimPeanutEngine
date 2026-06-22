#ifndef KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H

#include <memory>
#include <vector>
#include "miniaudio/miniaudio.h"
#include "audio_system.h"

namespace kpengine::audio
{
    namespace
    {
        static void DataCallback(
            ma_device *device,
            void *output,
            const void *input,
            ma_uint32 frame_count);
    }


    class MiniAudioSystem : public AudioSystem
    {
    public:
        bool Initialize() override;
        void ShutDown() override;
        AudioPlayer *CreateAudioPlayer() override;
        void Mix(float* source, uint32_t frame_count) override;
        MiniAudioSystem();
        ~MiniAudioSystem();
    private:
        ma_device device_;
        std::vector<std::unique_ptr<class AudioPlayer>> players_;
    };
}

#endif