#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_H

#include <memory>
#include "data/audioclip.h"

namespace kpengine::asset{
    using AudioClip = kpengine::data::AudioClip;

    struct AudioResource{
        std::shared_ptr<AudioClip> resource;
    };
}

#endif