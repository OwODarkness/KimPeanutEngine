#ifndef KPENGINE_MODULE_TTS_PROVIDER_H
#define KPENGINE_MODULE_TTS_PROVIDER_H

#include "types.h"
#include <cstdint>
#include <vector>
#include <functional>
namespace kpengine::tts
{

    using AudioDataCallback = std::function<bool(const uint8_t*, size_t)>;
    using ErrorCallback     = std::function<void(const std::string& )>;
    using FinishCallback = std::function<void()>;

    class ITTSProvider
    {
    public:
        virtual ~ITTSProvider() = default;
        virtual bool Initialize(const ServerConfig &config) = 0;
        virtual void ShutDown() = 0;
        virtual bool SynthesizeBuffer(const TTSRequest& request, AudioDataCallback OnData, ErrorCallback OnError) = 0;
        virtual bool SynthesizeStream(const TTSRequest& request, AudioDataCallback OnData, FinishCallback OnFinish,  ErrorCallback OnError) = 0;

    };
}

#endif