#ifndef KPENGINE_RUNTIME_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_AUDIO_SYSTEM_H


#include <cstdint>
#include <vector>
#include <memory>
#include "base/handle.h"

namespace kpengine::audio{
    class AudioPlayer;
    struct AudioTag{};
    using AudioHandle = Handle<AudioTag>;

    class AudioSystem{
    public:
        AudioSystem();
        virtual  ~AudioSystem();
        virtual bool Initialize() = 0;
        virtual void ShutDown() = 0;
        virtual void Mix(float* source, uint32_t frame_count) = 0;

        virtual AudioPlayer* GetAudioPlayer(AudioHandle handle);
        virtual AudioHandle CreateAudioPlayer() ;
        virtual bool DestroyAudioPlayer(AudioHandle handle);
    protected:
        HandleSystem<AudioHandle> handle_system_;
        std::vector<std::unique_ptr<class AudioPlayer>> players_;

    };
}

#endif