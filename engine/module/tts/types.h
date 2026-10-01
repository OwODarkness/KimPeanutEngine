#ifndef KPENGINE_MODULE_TTS_TYPES_H
#define KPENGINE_MODULE_TTS_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include "data/audio.h"
#include "audio/audio_types.h"

namespace kpengine::tts
{

    struct JobToken
    {
        uint64_t value = 0;
        bool IsValid() const { return value != 0; }
        friend bool operator==(JobToken, JobToken) = default;
    };

    enum class TTSJobState : uint8_t
    {
        Queued,
        Connecting,
        Generating,
        Buffering,
        Playing,
        Draining,
        Completed,
        Cancelled,
        Failed
    };

    enum class TTSJobEventType : uint8_t { StateChanged, NetworkFinished };

    struct TTSJobEvent
    {
        TTSJobEventType type = TTSJobEventType::StateChanged;
        JobToken job{};
        uint64_t turn_id = 0;
        TTSJobState state = TTSJobState::Queued;
        std::chrono::steady_clock::time_point timestamp{};
        std::string message;
    };

    struct TTSJobTelemetry
    {
        JobToken job{};
        uint64_t turn_id = 0;
        TTSJobState state = TTSJobState::Queued;
        uint64_t queue_occupancy = 0;
        uint64_t underrun_blocks = 0;
        std::chrono::milliseconds request_to_first_audio{0};
        std::chrono::milliseconds synthesis_elapsed{0};
        std::chrono::milliseconds audible_drain{0};
    };

    using AudioClip_SharedPtr = std::shared_ptr<data::AudioClip>;

    enum class TTSProviderType
    {
        GPT_SOVITS
    };
    struct ServerConfig
    {
        std::string host;
        uint32_t port = 0;
        std::string api_path;
        uint32_t timeout = 0;
    };

    struct TTSRequest
    {
        uint64_t turn_id = 0;
        std::string ref_audio_path;
        std::string prompt_text;
        std::string prompt_lang;
        std::string text;
        std::string text_lang;
        bool streaming = false;
    };

    struct TTSResult
    {
        bool success = false;
        int32_t error_code = 0;
        audio::AudioHandle player_handle;
        std::string error_message;
        JobToken job{};
        uint64_t turn_id = 0;
    };
}

#endif
