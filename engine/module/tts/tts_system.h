#ifndef KPENGINE_MODULE_TTS_H
#define KPENGINE_MODULE_TTS_H

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <queue>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>
#include "types.h"
#include "tts_provider.h"
#include "asset/audio_loader.h"


using IAudioLoader = kpengine::asset::IAudioLoader;

namespace kpengine::audio{
    class AudioSystem;
}

namespace kpengine::tts
{

    class TTSSystem
    {
    public:
        TTSSystem();
        ~TTSSystem();
        bool Initialize(
            TTSProviderType type, const ServerConfig &config);
        bool InitializeWithProvider(std::unique_ptr<ITTSProvider> provider,
                                   const ServerConfig& config);
        void ShutDown();

        TTSResult SyncSynthesize(const TTSRequest &request);

        JobToken AsyncSynthesize(
            const TTSRequest &request,
            std::function<void(const TTSResult &)> callback);
        bool Cancel(JobToken job);
        std::vector<TTSJobEvent> DrainEvents();
        std::vector<TTSJobTelemetry> GetJobTelemetry() const;
    private:
        struct JobContext
        {
            JobToken token{};
            TTSRequest request;
            std::function<void(const TTSResult&)> callback;
            std::atomic<bool> cancelled{false};
            std::atomic<bool> terminal{false};
            std::atomic<bool> synthesis_finished{false};
            std::atomic<bool> network_finished{false};
            std::atomic<bool> callback_sent{false};
            TTSJobTelemetry telemetry{};
            audio::AudioHandle audio_handle{};
            std::chrono::steady_clock::time_point created_at{};
            std::chrono::steady_clock::time_point synthesis_started_at{};
            std::chrono::steady_clock::time_point synthesis_finished_at{};
            std::chrono::steady_clock::time_point network_finished_at{};
            std::chrono::steady_clock::time_point first_audio_at{};
            uint64_t starting_underrun_blocks = 0;
        };

        void WorkerLoop();
        void MonitorLoop();
        TTSResult Synthesize(const TTSRequest& request, const std::shared_ptr<JobContext>& job);
        void Transition(const std::shared_ptr<JobContext>& job, TTSJobState state,
                        std::string message = {});
        void EmitNetworkFinished(const std::shared_ptr<JobContext>& job);
        void CompleteCallback(const std::shared_ptr<JobContext>& job, const TTSResult& result);

    public:
        audio::AudioSystem* audio_system = nullptr;
    private:
        std::unique_ptr<ITTSProvider> provider_;

        std::atomic<bool> initialized{false};

        std::queue<std::shared_ptr<JobContext>> tasks_;
        std::thread worker_;
        std::thread monitor_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
    
        bool running_ = false;
        uint64_t next_job_id_ = 1;
        std::unordered_map<uint64_t, std::shared_ptr<JobContext>> jobs_;
        std::vector<TTSJobEvent> events_;
        std::vector<TTSJobTelemetry> recent_telemetry_;

        std::unique_ptr<IAudioLoader> audio_loader_;
    };

}

#endif
