#include "tts_system.h"

#include "asset/miniaudio_audio_loader.h"
#include "gpt_sovits_tts.h"
#include "audio/audio_stream_decoder.h"
#include "audio/audio_system.h"
#include "audio/buffer_audio_player.h"
#include "audio/stream_audio_player.h"
#include "audio/audio_stream.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace kpengine::tts
{
    namespace
    {
        constexpr uint32_t kStreamBufferSeconds = 20;
        constexpr size_t kMaxQueuedTtsTasks = 16;
        constexpr auto kMonitorPeriod = std::chrono::milliseconds(10);

        template <typename Callback>
        class ScopeExit
        {
        public:
            explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
            ~ScopeExit() { if (active_) callback_(); }
            ScopeExit(const ScopeExit&) = delete;
            ScopeExit& operator=(const ScopeExit&) = delete;
            void Release() { active_ = false; }

        private:
            Callback callback_;
            bool active_ = true;
        };

        template <typename Callback>
        ScopeExit(Callback) -> ScopeExit<Callback>;

        bool IsTerminal(TTSJobState state)
        {
            return state == TTSJobState::Completed || state == TTSJobState::Cancelled ||
                   state == TTSJobState::Failed;
        }
    }

    TTSSystem::TTSSystem()
        : audio_loader_(std::make_unique<kpengine::asset::MiniAudio_AudioLoader>())
    {
    }

    bool TTSSystem::Initialize(TTSProviderType type, const ServerConfig& config)
    {
        std::unique_ptr<ITTSProvider> provider;
        switch (type)
        {
        case TTSProviderType::GPT_SOVITS:
            provider = std::make_unique<GPTSovitsTTS>();
            break;
        }
        return InitializeWithProvider(std::move(provider), config);
    }

    bool TTSSystem::InitializeWithProvider(std::unique_ptr<ITTSProvider> provider,
                                           const ServerConfig& config)
    {
        if (initialized || worker_.joinable() || !provider)
            return false;
        if (!provider->Initialize(config))
        {
            provider->ShutDown();
            return false;
        }

        provider_ = std::move(provider);
        {
            std::lock_guard lock(mutex_);
            initialized = true;
            running_ = true;
        }

        try
        {
            worker_ = std::thread(&TTSSystem::WorkerLoop, this);
            monitor_ = std::thread(&TTSSystem::MonitorLoop, this);
        }
        catch (...)
        {
            {
                std::lock_guard lock(mutex_);
                initialized = false;
                running_ = false;
            }
            cv_.notify_all();
            if (provider_)
                provider_->ShutDown();
            if (worker_.joinable())
                worker_.join();
            if (monitor_.joinable())
                monitor_.join();
            provider_.reset();
            return false;
        }
        return true;
    }

    void TTSSystem::ShutDown()
    {
        std::vector<std::shared_ptr<JobContext>> jobs;
        {
            std::lock_guard lock(mutex_);
            if (!initialized && !worker_.joinable() && !monitor_.joinable())
                return;
            initialized = false;
            running_ = false;
            jobs.reserve(jobs_.size());
            for (const auto& [id, job] : jobs_)
            {
                (void)id;
                job->cancelled.store(true, std::memory_order_release);
                jobs.push_back(job);
            }
        }
        cv_.notify_all();

        for (const auto& job : jobs)
            if (provider_)
                provider_->Cancel(job->token);
        if (provider_)
            provider_->ShutDown();
        if (worker_.joinable())
            worker_.join();

        for (const auto& job : jobs)
        {
            audio::AudioHandle handle;
            {
                std::lock_guard lock(mutex_);
                handle = job->audio_handle;
            }
            if (audio_system && handle.IsValid())
                audio_system->DestroyAudioPlayer(handle);
            Transition(job, TTSJobState::Cancelled, "TTS system shut down");
            TTSResult result;
            result.job = job->token;
            result.turn_id = job->request.turn_id;
            result.error_code = -8;
            result.error_message = "TTS system shut down";
            CompleteCallback(job, result);
        }

        if (monitor_.joinable())
            monitor_.join();
        {
            std::lock_guard lock(mutex_);
            jobs_.clear();
            while (!tasks_.empty())
                tasks_.pop();
        }
        provider_.reset();
    }

    void TTSSystem::WorkerLoop()
    {
        while (true)
        {
            std::shared_ptr<JobContext> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });
                if (!running_)
                    break;
                job = std::move(tasks_.front());
                tasks_.pop();
            }
            if (job->cancelled.load(std::memory_order_acquire))
                continue;

            job->synthesis_started_at = std::chrono::steady_clock::now();
            Transition(job, TTSJobState::Connecting);
            Transition(job, TTSJobState::Generating);
            TTSResult result;
            try
            {
                result = Synthesize(job->request, job);
            }
            catch (const std::exception& exception)
            {
                result.error_code = -9;
                result.error_message = std::string("TTS provider failed: ") + exception.what();
                if (result.error_message.size() > 512)
                    result.error_message.resize(512);
            }
            catch (...)
            {
                result.error_code = -9;
                result.error_message = "TTS provider failed with an unknown error";
            }
            result.job = job->token;
            result.turn_id = job->request.turn_id;
            job->synthesis_finished_at = std::chrono::steady_clock::now();
            if (job->cancelled.load(std::memory_order_acquire))
                Transition(job, TTSJobState::Cancelled, "TTS job cancelled");
            else if (!result.success)
                Transition(job, TTSJobState::Failed, result.error_message);
            job->synthesis_finished.store(true, std::memory_order_release);
            if (job->network_finished.load(std::memory_order_acquire))
                EmitNetworkFinished(job);
            {
                std::lock_guard lock(mutex_);
                job->telemetry.synthesis_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    job->synthesis_finished_at - job->synthesis_started_at);
            }
            CompleteCallback(job, result);
        }

        std::vector<std::shared_ptr<JobContext>> queued;
        {
            std::lock_guard lock(mutex_);
            while (!tasks_.empty())
            {
                queued.push_back(std::move(tasks_.front()));
                tasks_.pop();
            }
        }
        for (const auto& job : queued)
        {
            TTSResult result;
            result.job = job->token;
            result.turn_id = job->request.turn_id;
            result.error_code = -8;
            result.error_message = "TTS system shut down before the request started";
            job->cancelled.store(true, std::memory_order_release);
            CompleteCallback(job, result);
            Transition(job, TTSJobState::Cancelled, result.error_message);
        }
    }

    void TTSSystem::MonitorLoop()
    {
        while (true)
        {
            std::vector<std::shared_ptr<JobContext>> jobs;
            bool running = false;
            {
                std::lock_guard lock(mutex_);
                running = running_;
                jobs.reserve(jobs_.size());
                for (const auto& [id, job] : jobs_)
                {
                    (void)id;
                    jobs.push_back(job);
                }
            }
            if (!running && jobs.empty())
                break;

            for (const auto& job : jobs)
            {
                if (job->terminal.load(std::memory_order_acquire))
                    continue;
                if (job->cancelled.load(std::memory_order_acquire))
                {
                    Transition(job, TTSJobState::Cancelled, "TTS job cancelled");
                    continue;
                }
                const bool synthesis_finished = job->synthesis_finished.load(std::memory_order_acquire);
                audio::AudioHandle handle;
                {
                    std::lock_guard lock(mutex_);
                    handle = job->audio_handle;
                }
                auto player = audio_system && handle.IsValid()
                    ? audio_system->GetAudioPlayer(handle) : nullptr;
                if (!player)
                {
                    if (!synthesis_finished)
                        continue;
                    Transition(job, TTSJobState::Failed, "TTS audio player is no longer available");
                    continue;
                }

                const auto player_state = player->GetCurrentState();
                if (player_state == audio::AudioState::Finished)
                {
                    if (!synthesis_finished)
                        continue;
                    const auto now = std::chrono::steady_clock::now();
                    {
                        std::lock_guard lock(mutex_);
                        job->telemetry.audible_drain = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - job->synthesis_finished_at);
                        if (audio_system)
                        {
                            const auto underruns = audio_system->GetTelemetrySnapshot()
                                .buses[static_cast<size_t>(audio::AudioBus::Speech)].underrun_blocks;
                            job->telemetry.underrun_blocks = underruns >= job->starting_underrun_blocks
                                ? underruns - job->starting_underrun_blocks : 0;
                        }
                    }
                    Transition(job, TTSJobState::Completed);
                    continue;
                }
                if (player_state == audio::AudioState::Cancelled)
                {
                    Transition(job, TTSJobState::Cancelled, "Audio playback cancelled");
                    continue;
                }
                if (synthesis_finished)
                    Transition(job, TTSJobState::Draining);
                else if (player_state == audio::AudioState::Playing)
                    Transition(job, TTSJobState::Playing);
                else
                    Transition(job, TTSJobState::Buffering);
            }
            std::this_thread::sleep_for(kMonitorPeriod);
        }
    }

    TTSResult TTSSystem::SyncSynthesize(const TTSRequest& request)
    {
        return Synthesize(request, {});
    }

    TTSResult TTSSystem::Synthesize(const TTSRequest& request,
                                    const std::shared_ptr<JobContext>& job)
    {
        TTSResult result;
        result.turn_id = request.turn_id;
        if (job)
            result.job = job->token;
        const auto is_cancelled = [job] {
            return job && job->cancelled.load(std::memory_order_acquire);
        };
        if (!initialized || !provider_)
        {
            result.error_code = -1;
            result.error_message = "TTS system is not initialized";
            return result;
        }
        if (!audio_system || !audio_system->IsInitialized())
        {
            result.error_code = -2;
            result.error_message = "Audio system is not configured or initialized";
            return result;
        }

        if (request.streaming)
        {
            const auto handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Stream);
            auto player = std::dynamic_pointer_cast<audio::StreamAudioPlayer>(audio_system->GetAudioPlayer(handle));
            if (!player)
            {
                audio_system->DestroyAudioPlayer(handle);
                result.error_code = -3;
                result.error_message = "Failed to create streaming audio player";
                return result;
            }
            auto release_player = ScopeExit([&] { audio_system->DestroyAudioPlayer(handle); });
            data::AudioFormat format;
            format.channels = 1;
            format.sample_rate = 48000;
            auto stream = std::make_shared<audio::AudioStream>(format, kStreamBufferSeconds);
            audio::AudioStreamDecoder decoder(stream);
            player->SetStream(stream);
            if (job)
            {
                std::lock_guard lock(mutex_);
                job->audio_handle = handle;
            }

            bool decoder_finished = false;
            const auto on_data = [&](const uint8_t* bytes, size_t size) {
                if (is_cancelled())
                    return false;
                const auto decode = decoder.Feed(bytes, size);
                if (decode == audio::AudioDecodeResult::InvalidData)
                    return false;
                if (job && decode == audio::AudioDecodeResult::DataDecoded &&
                    job->first_audio_at == std::chrono::steady_clock::time_point{})
                {
                    job->first_audio_at = std::chrono::steady_clock::now();
                    std::lock_guard lock(mutex_);
                    job->telemetry.request_to_first_audio = std::chrono::duration_cast<std::chrono::milliseconds>(
                        job->first_audio_at - job->created_at);
                }
                player->Play();
                return true;
            };
            const auto on_finish = [&] {
                decoder_finished = decoder.Finish();
                if (decoder_finished && job)
                {
                    job->network_finished_at = std::chrono::steady_clock::now();
                    job->network_finished.store(true, std::memory_order_release);
                }
            };
            const auto on_error = [&](const std::string& message) {
                result.error_code = -4;
                result.error_message = message.substr(0, 512);
            };
            const bool succeeded = provider_->SynthesizeStream(job ? job->token : JobToken{}, request,
                on_data, on_finish, on_error, is_cancelled);
            result.success = succeeded && decoder_finished && !is_cancelled();
            if (succeeded && !decoder_finished && result.error_message.empty())
            {
                result.error_code = -5;
                result.error_message = "TTS audio ended with an incomplete or invalid WAV stream";
            }
            if (result.success)
            {
                result.player_handle = handle;
                release_player.Release();
            }
            else
            {
                stream->Cancel();
                if (is_cancelled())
                {
                    result.error_code = -8;
                    result.error_message = "TTS job cancelled";
                }
            }
        }
        else
        {
            const auto handle = audio_system->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
            auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(audio_system->GetAudioPlayer(handle));
            if (!player)
            {
                audio_system->DestroyAudioPlayer(handle);
                result.error_code = -3;
                result.error_message = "Failed to create buffered audio player";
                return result;
            }
            auto release_player = ScopeExit([&] { audio_system->DestroyAudioPlayer(handle); });
            if (job)
            {
                std::lock_guard lock(mutex_);
                job->audio_handle = handle;
            }
            const auto on_data = [&](const uint8_t* bytes, size_t size) {
                if (is_cancelled())
                    return false;
                auto clip = audio_loader_->LoadFromMemory(reinterpret_cast<const char*>(bytes), size).data;
                if (!clip || clip->frame_count == 0 || clip->format.channels == 0 ||
                    clip->format.sample_rate == 0 || clip->pcm.empty())
                {
                    result.error_code = -5;
                    result.error_message = "TTS returned an invalid or empty audio clip";
                    return false;
                }
                if (job && job->first_audio_at == std::chrono::steady_clock::time_point{})
                {
                    job->first_audio_at = std::chrono::steady_clock::now();
                    std::lock_guard lock(mutex_);
                    job->telemetry.request_to_first_audio = std::chrono::duration_cast<std::chrono::milliseconds>(
                        job->first_audio_at - job->created_at);
                }
                player->SetClip(std::move(clip));
                player->Play();
                return true;
            };
            const auto on_error = [&](const std::string& message) {
                result.error_code = -4;
                result.error_message = message.substr(0, 512);
            };
            const bool succeeded = provider_->SynthesizeBuffer(job ? job->token : JobToken{}, request,
                on_data, on_error, is_cancelled);
            if (succeeded && job)
            {
                job->network_finished_at = std::chrono::steady_clock::now();
                job->network_finished.store(true, std::memory_order_release);
            }
            result.success = succeeded && !is_cancelled() && result.error_message.empty();
            if (result.success)
            {
                result.player_handle = handle;
                release_player.Release();
            }
            else
            {
                if (is_cancelled())
                {
                    result.error_code = -8;
                    result.error_message = "TTS job cancelled";
                }
                else if (result.error_message.empty())
                {
                    result.error_code = -4;
                    result.error_message = "TTS provider returned no playable audio";
                }
            }
        }
        return result;
    }

    JobToken TTSSystem::AsyncSynthesize(const TTSRequest& request,
                                        std::function<void(const TTSResult&)> callback)
    {
        auto job = std::make_shared<JobContext>();
        job->request = request;
        job->callback = std::move(callback);
        job->created_at = std::chrono::steady_clock::now();
        if (audio_system && audio_system->IsInitialized())
            job->starting_underrun_blocks = audio_system->GetTelemetrySnapshot()
                .buses[static_cast<size_t>(audio::AudioBus::Speech)].underrun_blocks;
        bool rejected = false;
        {
            std::lock_guard lock(mutex_);
            if (!initialized || !running_ || !job->callback || tasks_.size() >= kMaxQueuedTtsTasks)
                rejected = true;
            else
            {
                job->token.value = next_job_id_++;
                job->telemetry.job = job->token;
                job->telemetry.turn_id = request.turn_id;
                job->telemetry.state = TTSJobState::Queued;
                jobs_.emplace(job->token.value, job);
                tasks_.push(job);
                job->telemetry.queue_occupancy = tasks_.size();
                events_.push_back({TTSJobEventType::StateChanged, job->token, request.turn_id,
                                   TTSJobState::Queued, job->created_at, {}});
            }
        }
        if (rejected)
        {
            TTSResult result;
            result.turn_id = request.turn_id;
            result.error_code = -7;
            result.error_message = "TTS system is unavailable or its request queue is full";
            CompleteCallback(job, result);
            return {};
        }
        cv_.notify_one();
        return job->token;
    }

    bool TTSSystem::Cancel(JobToken token)
    {
        std::shared_ptr<JobContext> job;
        audio::AudioHandle handle;
        {
            std::lock_guard lock(mutex_);
            const auto found = jobs_.find(token.value);
            if (found == jobs_.end() || found->second->terminal.load(std::memory_order_acquire))
                return false;
            job = found->second;
            job->cancelled.store(true, std::memory_order_release);
            handle = job->audio_handle;
        }
        if (provider_)
            provider_->Cancel(token);
        if (audio_system && handle.IsValid())
            audio_system->DestroyAudioPlayer(handle);
        TTSResult result;
        result.job = token;
        result.turn_id = job->request.turn_id;
        result.error_code = -8;
        result.error_message = "TTS job cancelled";
        CompleteCallback(job, result);
        Transition(job, TTSJobState::Cancelled, result.error_message);
        cv_.notify_all();
        return true;
    }

    void TTSSystem::Transition(const std::shared_ptr<JobContext>& job, TTSJobState state,
                               std::string message)
    {
        if (!job)
            return;
        std::lock_guard lock(mutex_);
        if (job->terminal.load(std::memory_order_acquire))
            return;
        if (IsTerminal(state))
        {
            bool expected = false;
            if (!job->terminal.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;
        }
        if (job->telemetry.state == state)
            return;
        job->telemetry.state = state;
        events_.push_back({TTSJobEventType::StateChanged, job->token, job->request.turn_id,
                           state, std::chrono::steady_clock::now(), std::move(message)});
        if (IsTerminal(state))
        {
            constexpr size_t kRecentTelemetryLimit = 64;
            if (recent_telemetry_.size() == kRecentTelemetryLimit)
                recent_telemetry_.erase(recent_telemetry_.begin());
            recent_telemetry_.push_back(job->telemetry);
            jobs_.erase(job->token.value);
        }
    }

    void TTSSystem::EmitNetworkFinished(const std::shared_ptr<JobContext>& job)
    {
        std::lock_guard lock(mutex_);
        events_.push_back({TTSJobEventType::NetworkFinished, job->token, job->request.turn_id,
                           job->telemetry.state, job->network_finished_at, {}});
    }

    void TTSSystem::CompleteCallback(const std::shared_ptr<JobContext>& job, const TTSResult& result)
    {
        if (!job || !job->callback)
            return;
        bool expected = false;
        if (job->callback_sent.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            job->callback(result);
    }

    std::vector<TTSJobEvent> TTSSystem::DrainEvents()
    {
        std::lock_guard lock(mutex_);
        std::vector<TTSJobEvent> events;
        events.swap(events_);
        return events;
    }

    std::vector<TTSJobTelemetry> TTSSystem::GetJobTelemetry() const
    {
        std::lock_guard lock(mutex_);
        std::vector<TTSJobTelemetry> telemetry = recent_telemetry_;
        telemetry.reserve(telemetry.size() + jobs_.size());
        for (const auto& [id, job] : jobs_)
        {
            (void)id;
            auto current = job->telemetry;
            current.queue_occupancy = tasks_.size();
            telemetry.push_back(current);
        }
        return telemetry;
    }

    TTSSystem::~TTSSystem()
    {
        ShutDown();
    }
}
