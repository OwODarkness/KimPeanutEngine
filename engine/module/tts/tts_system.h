#ifndef KPENGINE_MODULE_TTS_H
#define KPENGINE_MODULE_TTS_H

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <queue>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "types.h"
#include "tts_provider.h"
#include "asset/audio_loader.h"


using IAudioLoader = kpengine::asset::IAudioLoader;

namespace kpengine::audio{
    class AudioSystem;
}

namespace kpengine::tts
{

    struct TTSTask
    {
        TTSRequest request;
        std::function<void(const TTSResult &)> callback;
    };

    class TTSSystem
    {
    public:
        TTSSystem();
        ~TTSSystem();
        bool Initialize(
            TTSProviderType type, const ServerConfig &config);
        void ShutDown();

        TTSResult SyncSynthesize(const TTSRequest &request);

        void AsyncSynthesize(
            const TTSRequest &request,
            std::function<void(const TTSResult &)> callback);
    private:
        void WorkerLoop();

    public:
        audio::AudioSystem* audio_system;
    private:
        std::unique_ptr<ITTSProvider> provider_;

        bool initialized = false;

        std::queue<TTSTask> tasks_;
        std::thread worker_;
        std::mutex mutex_;
        std::condition_variable cv_;
    
        bool running_ = false;

        std::unique_ptr<IAudioLoader> audio_loader_;
    };

}

#endif