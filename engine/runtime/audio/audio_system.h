#ifndef KPENGINE_RUNTIME_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_AUDIO_SYSTEM_H


#include <cstdint>

namespace kpengine::audio{
    class AudioPlayer;

    class AudioSystem{
    public:
        virtual  ~AudioSystem() = default;
        virtual bool Initialize() = 0;
        virtual void ShutDown() = 0;
        virtual void Mix(float* source, uint32_t frame_count) = 0;

        virtual AudioPlayer* CreateAudioPlayer() = 0;
    protected:
    };
}

#endif