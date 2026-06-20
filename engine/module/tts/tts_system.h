#ifndef KPENGINE_MODULE_TTS_H
#define KPENGINE_MODULE_TTS_H

#include <string>

namespace kpengine::tts{

    struct AudioClip{

    };

    class TTSSystem{
    public:
        virtual AudioClip Synthesis(const std::string& text) = 0;
    };


}

#endif