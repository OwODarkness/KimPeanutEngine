#ifndef KPENGINE_MODULE_TTS_H
#define KPENGINE_MODULE_TTS_H

#include <string>
#include <cstdint>
#include <vector>
namespace kpengine::tts{

    class TTSSystem{
    public:
        virtual std::vector<uint8_t> Synthesis(const std::string& text) = 0;
    };


}

#endif