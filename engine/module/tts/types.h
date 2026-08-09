#ifndef KPENGINE_MODULE_TTS_TYPES_H
#define KPENGINE_MODULE_TTS_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "data/audio.h"
#include "audio/audio_types.h"

namespace kpengine::tts
{

    using AudioClip_SharedPtr = std::shared_ptr<data::AudioClip>;

    enum class TTSProviderType
    {
        GPT_SOVITS
    };
    struct ServerConfig
    {
        std::string host;
        uint32_t port;
        std::string api_path;
        uint32_t timeout;
    };

    struct TTSRequest
    {
        std::string ref_audio_path;
        std::string prompt_text;
        std::string prompt_lang;
        std::string text;
        std::string text_lang;
        bool streaming;
    };

    struct TTSResult
    {
        bool success = false;
        int32_t error_code;
        audio::AudioHandle player_handle;
        std::string error_message;
    };
}

#endif