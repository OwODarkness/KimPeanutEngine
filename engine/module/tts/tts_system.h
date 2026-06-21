#ifndef KPENGINE_MODULE_TTS_H
#define KPENGINE_MODULE_TTS_H

#include <string>
#include "audio/audio_clip.h"

namespace kpengine::tts{
    using AudioClip = kpengine::audio::AudioClip;

    class TTSSystem{
    public:
        virtual AudioClip Synthesis(const std::string& text) = 0;
    };


}

#endif