#ifndef KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_MINI_AUDIO_SYSTEM_H

#include <memory>

#include "audio_system.h"
namespace kpengine::audio
{
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
        class MiniAudioWrapper;
        std::unique_ptr<MiniAudioWrapper> wrapper_;
    };
}

#endif