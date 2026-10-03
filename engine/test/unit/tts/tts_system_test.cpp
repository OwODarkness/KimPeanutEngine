#include "module/tts/tts_system.h"
#include "runtime/audio/audio_system.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    void WriteU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
    {
        bytes[offset] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
    }

    void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        for (size_t byte = 0; byte < 4; ++byte)
            bytes[offset + byte] = static_cast<uint8_t>((value >> (byte * 8u)) & 0xffu);
    }

    std::vector<uint8_t> MakeWav()
    {
        constexpr uint32_t frame_count = 480;
        constexpr uint32_t data_bytes = frame_count * 2;
        std::vector<uint8_t> bytes(44 + data_bytes, 0);
        std::memcpy(bytes.data(), "RIFF", 4);
        WriteU32(bytes, 4, static_cast<uint32_t>(bytes.size() - 8));
        std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
        WriteU32(bytes, 16, 16);
        WriteU16(bytes, 20, 1);
        WriteU16(bytes, 22, 1);
        WriteU32(bytes, 24, 48000);
        WriteU32(bytes, 28, 96000);
        WriteU16(bytes, 32, 2);
        WriteU16(bytes, 34, 16);
        std::memcpy(bytes.data() + 36, "data", 4);
        WriteU32(bytes, 40, data_bytes);
        return bytes;
    }

    class TestAudioSystem final : public kpengine::audio::AudioSystem
    {
    public:
        bool Initialize() override { initialized_ = true; return true; }
        void ShutDown() override { initialized_ = false; }
        bool IsInitialized() const override { return initialized_; }

    private:
        bool initialized_ = false;
    };

    class BlockingProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        bool Initialize(const kpengine::tts::ServerConfig&) override { return true; }
        void ShutDown() override { Cancel({}); }
        void Cancel(kpengine::tts::JobToken job) override
        {
            std::lock_guard lock(mutex_);
            if (!job.IsValid() || job == active_job_)
                cancelled_ = true;
            cv_.notify_all();
        }
        bool SynthesizeBuffer(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback, kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            return false;
        }
        bool SynthesizeStream(kpengine::tts::JobToken job, const kpengine::tts::TTSRequest& request,
                              kpengine::tts::AudioDataCallback, kpengine::tts::FinishCallback,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck is_cancelled) override
        {
            std::unique_lock lock(mutex_);
            active_job_ = job;
            ++started_count_;
            cv_.notify_all();
            if (request.turn_id == 1003)
                return false;
            cv_.wait(lock, [&] { return cancelled_ || is_cancelled(); });
            return false;
        }

        void WaitUntilStarted(size_t count = 1)
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return started_count_ >= count; });
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        kpengine::tts::JobToken active_job_{};
        size_t started_count_ = 0;
        bool cancelled_ = false;
    };

    class ImmediateAudioProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        bool Initialize(const kpengine::tts::ServerConfig&) override { return true; }
        void ShutDown() override {}
        void Cancel(kpengine::tts::JobToken) override {}
        bool SynthesizeBuffer(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback on_data, kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck is_cancelled) override
        {
            const auto wav = MakeWav();
            return !is_cancelled() && on_data(wav.data(), wav.size());
        }
        bool SynthesizeStream(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback on_data, kpengine::tts::FinishCallback on_finish,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck is_cancelled) override
        {
            const auto wav = MakeWav();
            if (is_cancelled() || !on_data(wav.data(), wav.size())) return false;
            on_finish();
            return true;
        }
    };

    class OversizedProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        bool Initialize(const kpengine::tts::ServerConfig&) override { return true; }
        void ShutDown() override {}
        void Cancel(kpengine::tts::JobToken) override {}
        bool SynthesizeBuffer(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback on_data,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            std::vector<uint8_t> bytes(32u * 1024u * 1024u + 1u, 0);
            return on_data(bytes.data(), bytes.size());
        }
        bool SynthesizeStream(kpengine::tts::JobToken,
                              const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback,
                              kpengine::tts::FinishCallback,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            return false;
        }
    };

    class LifecycleProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        bool Initialize(const kpengine::tts::ServerConfig&) override { return true; }
        void ShutDown() override
        {
            std::lock_guard lock(mutex_);
            release_first_ = release_playing_ = true;
            cv_.notify_all();
        }
        void Cancel(kpengine::tts::JobToken) override
        {
            std::lock_guard lock(mutex_);
            cv_.notify_all();
        }
        bool SynthesizeBuffer(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback, kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            return false;
        }
        bool SynthesizeStream(kpengine::tts::JobToken, const kpengine::tts::TTSRequest& request,
                              kpengine::tts::AudioDataCallback on_data,
                              kpengine::tts::FinishCallback on_finish,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck is_cancelled) override
        {
            if (request.turn_id == 3000)
            {
                const auto wav = MakeWav();
                if (!on_data(wav.data(), wav.size()))
                    return false;
                std::unique_lock lock(mutex_);
                buffering_started_ = true;
                cv_.notify_all();
                cv_.wait(lock, [&] { return is_cancelled(); });
                return false;
            }
            if (request.turn_id == 3001)
            {
                std::unique_lock lock(mutex_);
                first_started_ = true;
                cv_.notify_all();
                cv_.wait(lock, [&] { return release_first_; });
                lock.unlock();
                const auto wav = MakeWav();
                on_data(wav.data(), wav.size());
                on_finish();
                return true;
            }
            if (request.turn_id == 3002)
            {
                const auto wav = MakeWav();
                if (!on_data(wav.data(), wav.size()))
                    return false;
                std::unique_lock lock(mutex_);
                playing_started_ = true;
                cv_.notify_all();
                cv_.wait(lock, [&] { return release_playing_; });
                lock.unlock();
                on_finish();
                return true;
            }
            const auto wav = MakeWav();
            const bool accepted = on_data(wav.data(), wav.size());
            if (accepted)
                on_finish();
            return accepted;
        }

        void WaitForFirst()
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return first_started_; });
        }
        void WaitForBufferingData()
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return buffering_started_; });
        }
        void ReleaseFirst()
        {
            std::lock_guard lock(mutex_);
            release_first_ = true;
            cv_.notify_all();
        }
        void WaitForPlayingData()
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return playing_started_; });
        }
        void ReleasePlaying()
        {
            std::lock_guard lock(mutex_);
            release_playing_ = true;
            cv_.notify_all();
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool first_started_ = false;
        bool buffering_started_ = false;
        bool release_first_ = false;
        bool playing_started_ = false;
        bool release_playing_ = false;
    };

    class UnderrunProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        bool Initialize(const kpengine::tts::ServerConfig&) override { return true; }
        void ShutDown() override { Release(); }
        void Cancel(kpengine::tts::JobToken) override {}
        bool SynthesizeBuffer(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback, kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            return false;
        }
        bool SynthesizeStream(kpengine::tts::JobToken, const kpengine::tts::TTSRequest&,
                              kpengine::tts::AudioDataCallback on_data,
                              kpengine::tts::FinishCallback on_finish,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck is_cancelled) override
        {
            const auto wav = MakeWav();
            constexpr size_t first_chunk_bytes = 44 + 200;
            if (!on_data(wav.data(), first_chunk_bytes))
                return false;
            {
                std::unique_lock lock(mutex_);
                first_chunk_sent_ = true;
                cv_.notify_all();
                cv_.wait(lock, [&] { return released_ || is_cancelled(); });
            }
            if (is_cancelled())
                return false;
            if (!on_data(wav.data() + first_chunk_bytes, wav.size() - first_chunk_bytes))
                return false;
            on_finish();
            return true;
        }
        void WaitForFirstChunk()
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return first_chunk_sent_; });
        }
        void Release()
        {
            std::lock_guard lock(mutex_);
            released_ = true;
            cv_.notify_all();
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool first_chunk_sent_ = false;
        bool released_ = false;
    };
}

TEST(TTSSystemTest, CancelsActiveAndQueuedJobsOnce)
{
    using namespace kpengine::tts;
    TestAudioSystem audio;
    ASSERT_TRUE(audio.Initialize());
    TTSSystem system;
    system.audio_system = &audio;
    auto provider = std::make_unique<BlockingProvider>();
    auto* provider_ptr = provider.get();
    ASSERT_TRUE(system.InitializeWithProvider(std::move(provider), {}));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    size_t callback_count = 0;
    const auto callback = [&](const TTSResult&) {
        std::lock_guard lock(callback_mutex);
        ++callback_count;
        callback_cv.notify_all();
    };

    TTSRequest request;
    request.streaming = true;
    request.turn_id = 1001;
    const JobToken active = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(active.IsValid());
    provider_ptr->WaitUntilStarted();

    request.turn_id = 1002;
    const JobToken queued = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(queued.IsValid());
    EXPECT_TRUE(system.Cancel(queued));
    EXPECT_FALSE(system.Cancel(queued));
    request.turn_id = 1003;
    const JobToken next = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(next.IsValid());
    EXPECT_TRUE(system.Cancel(active));

    provider_ptr->WaitUntilStarted(2);
    {
        std::unique_lock lock(callback_mutex);
        ASSERT_TRUE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return callback_count == 3;
        }));
    }

    const auto events = system.DrainEvents();
    EXPECT_EQ(std::count_if(events.begin(), events.end(), [&](const TTSJobEvent& event) {
        return event.type == TTSJobEventType::StateChanged && event.job == active &&
               event.state == TTSJobState::Cancelled;
    }), 1);
    EXPECT_EQ(std::count_if(events.begin(), events.end(), [&](const TTSJobEvent& event) {
        return event.type == TTSJobEventType::StateChanged && event.job == queued &&
               event.state == TTSJobState::Cancelled;
    }), 1);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [&](const TTSJobEvent& event) {
        return event.type == TTSJobEventType::StateChanged && event.job == next &&
               event.state == TTSJobState::Failed;
    }));

    system.ShutDown();
    audio.ShutDown();
}

TEST(TTSSystemTest, ReportsCompletionOnlyAfterAudibleDrain)
{
    using namespace kpengine::tts;
    TestAudioSystem audio;
    ASSERT_TRUE(audio.Initialize());
    TTSSystem system;
    system.audio_system = &audio;
    ASSERT_TRUE(system.InitializeWithProvider(std::make_unique<ImmediateAudioProvider>(), {}));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool callback_finished = false;
    TTSResult synthesis_result;
    TTSRequest request;
    request.turn_id = 2001;
    const auto job = system.AsyncSynthesize(request, [&](const TTSResult& result) {
        std::lock_guard lock(callback_mutex);
        synthesis_result = result;
        callback_finished = true;
        callback_cv.notify_all();
    });
    ASSERT_TRUE(job.IsValid());
    {
        std::unique_lock lock(callback_mutex);
        ASSERT_TRUE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return callback_finished;
        }));
    }
    ASSERT_TRUE(synthesis_result.success);

    std::array<float, kpengine::audio::AudioSystem::kMaxCallbackFrames * 2> output{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool completed = false;
    std::vector<TTSJobEvent> observed_events;
    while (std::chrono::steady_clock::now() < deadline && !completed)
    {
        audio.Mix(output.data(), kpengine::audio::AudioSystem::kMaxCallbackFrames);
        const auto events = system.DrainEvents();
        observed_events.insert(observed_events.end(), events.begin(), events.end());
        completed = std::any_of(events.begin(), events.end(), [&](const TTSJobEvent& event) {
            return event.job == job && event.type == TTSJobEventType::StateChanged &&
                   event.state == TTSJobState::Completed;
        });
        if (!completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(completed);
    const auto network_event = std::find_if(observed_events.begin(), observed_events.end(), [&](const TTSJobEvent& event) {
        return event.job == job && event.type == TTSJobEventType::NetworkFinished;
    });
    const auto completed_event = std::find_if(observed_events.begin(), observed_events.end(), [&](const TTSJobEvent& event) {
        return event.job == job && event.type == TTSJobEventType::StateChanged &&
               event.state == TTSJobState::Completed;
    });
    ASSERT_NE(network_event, observed_events.end());
    ASSERT_NE(completed_event, observed_events.end());
    EXPECT_LT(network_event->timestamp, completed_event->timestamp);
    system.ShutDown();
    audio.ShutDown();
}

TEST(TTSSystemTest, CapturesImmutableWavForStreamAndBufferAndRejectsOversize)
{
    using namespace kpengine::tts;
    TestAudioSystem audio;
    ASSERT_TRUE(audio.Initialize());
    TTSSystem system;
    system.audio_system = &audio;
    ASSERT_TRUE(system.InitializeWithProvider(std::make_unique<ImmediateAudioProvider>(), {}));
    const auto expected = MakeWav();
    std::array<float, kpengine::audio::AudioSystem::kMaxCallbackFrames * 2> output{};

    for (const bool streaming : {false, true})
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool received = false;
        TTSResult result;
        TTSRequest request;
        request.turn_id = streaming ? 2102 : 2101;
        request.streaming = streaming;
        const auto job = system.AsyncSynthesize(request, [&](const TTSResult &value) {
            std::lock_guard lock(mutex);
            result = value;
            received = true;
            cv.notify_all();
        });
        ASSERT_TRUE(job.IsValid());
        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return received; }));
        }
        ASSERT_TRUE(result.success) << result.error_message;
        ASSERT_NE(result.wav_bytes, nullptr);
        EXPECT_EQ(*result.wav_bytes, expected);
        EXPECT_EQ(result.wav_bytes.use_count(), 1);

        bool completed = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!completed && std::chrono::steady_clock::now() < deadline)
        {
            audio.Mix(output.data(), kpengine::audio::AudioSystem::kMaxCallbackFrames);
            for (const auto &event : system.DrainEvents())
                completed |= event.job == job && event.type == TTSJobEventType::StateChanged &&
                    event.state == TTSJobState::Completed;
            if (!completed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_TRUE(completed);
        audio.DestroyAudioPlayer(result.player_handle);
    }
    system.ShutDown();

    TTSSystem oversized;
    oversized.audio_system = &audio;
    ASSERT_TRUE(oversized.InitializeWithProvider(std::make_unique<OversizedProvider>(), {}));
    std::mutex mutex;
    std::condition_variable cv;
    bool received = false;
    TTSResult result;
    TTSRequest request;
    request.turn_id = 2103;
    const auto job = oversized.AsyncSynthesize(request, [&](const TTSResult &value) {
        std::lock_guard lock(mutex);
        result = value;
        received = true;
        cv.notify_all();
    });
    ASSERT_TRUE(job.IsValid());
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return received; }));
    }
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, -10);
    EXPECT_EQ(result.wav_bytes, nullptr);
    EXPECT_FALSE(result.player_handle.IsValid());
    oversized.ShutDown();
    audio.ShutDown();
}

TEST(TTSSystemTest, LateCancelledCallbacksCannotFinishOrContaminateTheNextTurn)
{
    using namespace kpengine::tts;
    TestAudioSystem audio;
    ASSERT_TRUE(audio.Initialize());
    TTSSystem system;
    system.audio_system = &audio;
    auto provider = std::make_unique<LifecycleProvider>();
    auto* provider_ptr = provider.get();
    ASSERT_TRUE(system.InitializeWithProvider(std::move(provider), {}));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    std::vector<TTSResult> results;
    const auto callback = [&](const TTSResult& result) {
        std::lock_guard lock(callback_mutex);
        results.push_back(result);
        callback_cv.notify_all();
    };

    TTSRequest request;
    request.streaming = true;
    request.turn_id = 3000;
    const JobToken buffering = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(buffering.IsValid());
    provider_ptr->WaitForBufferingData();
    const auto buffering_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    bool observed_buffering = false;
    while (std::chrono::steady_clock::now() < buffering_deadline && !observed_buffering)
    {
        const auto telemetry = system.GetJobTelemetry();
        observed_buffering = std::any_of(telemetry.begin(), telemetry.end(), [&](const auto& item) {
            return item.job == buffering && item.state == TTSJobState::Buffering;
        });
        if (!observed_buffering)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(observed_buffering);
    ASSERT_TRUE(system.Cancel(buffering));

    request.turn_id = 3001;
    const JobToken generating = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(generating.IsValid());
    provider_ptr->WaitForFirst();
    ASSERT_TRUE(system.Cancel(generating));

    request.turn_id = 3002;
    const JobToken playing = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(playing.IsValid());
    provider_ptr->ReleaseFirst();
    provider_ptr->WaitForPlayingData();

    const auto playing_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::array<float, 128> output{};
    bool observed_playing = false;
    while (std::chrono::steady_clock::now() < playing_deadline && !observed_playing)
    {
        audio.Mix(output.data(), 64);
        const auto telemetry = system.GetJobTelemetry();
        observed_playing = std::any_of(telemetry.begin(), telemetry.end(), [&](const auto& item) {
            return item.job == playing && item.state == TTSJobState::Playing;
        });
        if (!observed_playing)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(observed_playing);
    ASSERT_TRUE(system.Cancel(playing));

    request.turn_id = 3003;
    const JobToken draining = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(draining.IsValid());
    provider_ptr->ReleasePlaying();
    {
        std::unique_lock lock(callback_mutex);
        ASSERT_TRUE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::any_of(results.begin(), results.end(), [&](const TTSResult& result) {
                return result.job == draining;
            });
        }));
    }

    const auto draining_result = std::find_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == draining;
    });
    ASSERT_NE(draining_result, results.end());
    ASSERT_TRUE(draining_result->success);

    const auto draining_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool observed_draining = false;
    while (std::chrono::steady_clock::now() < draining_deadline && !observed_draining)
    {
        const auto telemetry = system.GetJobTelemetry();
        observed_draining = std::any_of(telemetry.begin(), telemetry.end(), [&](const auto& item) {
            return item.job == draining && item.state == TTSJobState::Draining;
        });
        if (!observed_draining)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(observed_draining);
    ASSERT_TRUE(system.Cancel(draining));

    request.turn_id = 3004;
    const JobToken next = system.AsyncSynthesize(request, callback);
    ASSERT_TRUE(next.IsValid());
    {
        std::unique_lock lock(callback_mutex);
        ASSERT_TRUE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::any_of(results.begin(), results.end(), [&](const TTSResult& result) {
                return result.job == next;
            });
        }));
    }
    const auto next_result = std::find_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == next;
    });
    ASSERT_NE(next_result, results.end());
    ASSERT_TRUE(next_result->success);

    bool completed_next = false;
    std::vector<TTSJobEvent> observed_events;
    const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < drain_deadline && !completed_next)
    {
        audio.Mix(output.data(), 64);
        const auto events = system.DrainEvents();
        observed_events.insert(observed_events.end(), events.begin(), events.end());
        completed_next = std::any_of(events.begin(), events.end(), [&](const TTSJobEvent& event) {
            return event.job == next && event.type == TTSJobEventType::StateChanged &&
                   event.state == TTSJobState::Completed;
        });
        if (!completed_next)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(completed_next);
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == generating;
    }), 1);
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == playing;
    }), 1);
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == draining;
    }), 1);
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [&](const TTSResult& result) {
        return result.job == next;
    }), 1);
    for (const JobToken cancelled : {buffering, generating, playing, draining})
    {
        EXPECT_EQ(std::count_if(observed_events.begin(), observed_events.end(), [&](const TTSJobEvent& event) {
            return event.job == cancelled && event.type == TTSJobEventType::StateChanged &&
                   event.state == TTSJobState::Cancelled;
        }), 1);
        EXPECT_FALSE(std::any_of(observed_events.begin(), observed_events.end(), [&](const TTSJobEvent& event) {
            return event.job == cancelled && event.type == TTSJobEventType::StateChanged &&
                   event.state == TTSJobState::Completed;
        }));
    }
    EXPECT_TRUE(std::none_of(observed_events.begin(), observed_events.end(), [&](const TTSJobEvent& event) {
        return (event.job == buffering || event.job == generating || event.job == playing) &&
               event.type == TTSJobEventType::NetworkFinished;
    }));

    system.ShutDown();
    audio.ShutDown();
}

TEST(TTSSystemTest, RecordsUnderrunAndResumesPlaybackAfterMoreAudioArrives)
{
    using namespace kpengine::tts;
    TestAudioSystem audio;
    ASSERT_TRUE(audio.Initialize());
    TTSSystem system;
    system.audio_system = &audio;
    auto provider = std::make_unique<UnderrunProvider>();
    auto* provider_ptr = provider.get();
    ASSERT_TRUE(system.InitializeWithProvider(std::move(provider), {}));

    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    bool callback_received = false;
    TTSResult result;
    TTSRequest request;
    request.turn_id = 4001;
    request.streaming = true;
    const JobToken job = system.AsyncSynthesize(request, [&](const TTSResult& value) {
        std::lock_guard lock(callback_mutex);
        result = value;
        callback_received = true;
        callback_cv.notify_all();
    });
    ASSERT_TRUE(job.IsValid());
    provider_ptr->WaitForFirstChunk();

    const auto buffering_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    bool observed_buffering = false;
    while (std::chrono::steady_clock::now() < buffering_deadline && !observed_buffering)
    {
        const auto telemetry = system.GetJobTelemetry();
        observed_buffering = std::any_of(telemetry.begin(), telemetry.end(), [&](const auto& item) {
            return item.job == job && item.state == TTSJobState::Buffering;
        });
        if (!observed_buffering)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(observed_buffering);

    std::array<float, 128 * 2> output{};
    audio.Mix(output.data(), 64);
    audio.Mix(output.data(), 64);
    const size_t speech_bus = static_cast<size_t>(kpengine::audio::AudioBus::Speech);
    EXPECT_GT(audio.GetTelemetrySnapshot().buses[speech_bus].underrun_blocks, 0u);
    provider_ptr->Release();
    {
        std::unique_lock lock(callback_mutex);
        ASSERT_TRUE(callback_cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return callback_received;
        }));
    }
    ASSERT_TRUE(result.success) << result.error_message;

    bool completed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !completed)
    {
        audio.Mix(output.data(), 64);
        const auto events = system.DrainEvents();
        completed = std::any_of(events.begin(), events.end(), [&](const TTSJobEvent& event) {
            return event.job == job && event.type == TTSJobEventType::StateChanged &&
                   event.state == TTSJobState::Completed;
        });
        if (!completed)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(completed);
    const auto telemetry = system.GetJobTelemetry();
    const auto final = std::find_if(telemetry.begin(), telemetry.end(), [&](const auto& item) {
        return item.job == job;
    });
    ASSERT_NE(final, telemetry.end());
    EXPECT_GE(final->underrun_blocks, 1u);

    system.ShutDown();
    audio.ShutDown();
}
