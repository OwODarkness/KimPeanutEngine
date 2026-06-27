#ifndef KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H

#include <miniaudio/miniaudio.h>
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
        void Mix(float* source, uint32_t frame_count) override;
        MiniAudioSystem();
        ~MiniAudioSystem();
    private:
        void ClearOutputBuffer(float* source, uint32_t size);
    private:
        ma_device device_;
    };
}

#endif