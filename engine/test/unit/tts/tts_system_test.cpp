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
                              kpengine::tts::AudioDataCallback, kpengine::tts::FinishCallback,
                              kpengine::tts::ErrorCallback,
                              kpengine::tts::CancellationCheck) override
        {
            return false;
        }
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
