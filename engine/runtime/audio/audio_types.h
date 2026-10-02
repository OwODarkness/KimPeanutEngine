#ifndef KPENGINE_RUNTIME_AUDIO_TYPES_H
#define KPENGINE_RUNTIME_AUDIO_TYPES_H

#include "base/handle.h"


namespace kpengine::audio{

    struct AudioTag{};
    using AudioHandle = Handle<AudioTag>;

    enum class AudioState: uint8_t
    {
        Playing,
        Buffering,
        FadingOut,
        Paused,
        Stopped,
        Finished,
        Cancelled
    };

    enum class AudioBus : uint8_t
    {
        Speech,
        Music,
        Count
    };

    enum class AudioStreamState : uint8_t
    {
        Open,
        ProducerFinished,
        Cancelled,
        Drained
    };

    enum class AudioPlayerType: uint8_t{
        Buffer,
        Stream,
        Seekable
    };

}



#endif
