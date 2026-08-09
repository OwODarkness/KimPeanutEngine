#ifndef KPENGINE_RUNTIME_AUDIO_TYPES_H
#define KPENGINE_RUNTIME_AUDIO_TYPES_H

#include "base/handle.h"


namespace kpengine::audio{

    struct AudioTag{};
    using AudioHandle = Handle<AudioTag>;

    enum class AudioState: uint8_t
    {
        Playing,
        Paused,
        Stopped,
        Finished
    };

    enum class AudioPlayerType: uint8_t{
        Buffer,
        Stream
    };

}



#endif