#ifndef KPENGINE_RUNTIME_AUDIO_LISTENER_H
#define KPENGINE_RUNTIME_AUDIO_LISTENER_H

#include "math/math_header.h"

namespace kpengine::audio{
    struct AudioListenerData{
        Vector3f position;
        Vector3f forward;
        Vector3f up;
    }
}

#endif