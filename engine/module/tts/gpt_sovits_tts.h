#ifndef KPENGINE_MODULE_GPT_SOVITS_TTS_H
#define KPENGINE_MODULE_GPT_SOVITS_TTS_H

#include "tts_system.h"

namespace kpengine::tts
{

    struct GPTSovitsConfig{
        std::string host;
        uint32_t port;
        std::string api_path;
        std::string ref_audio_path;
        std::string prompt_text;
        std::string prompt_lang;
        int timeout;
    };

    class GPTSovitsTTS : public TTSSystem
    {
    public:
        void LoadConfig(const GPTSovitsConfig& config);
        std::vector<uint8_t>  Synthesis(const std::string &text) override;

    private:
        GPTSovitsConfig config_;
    };
}

#endif