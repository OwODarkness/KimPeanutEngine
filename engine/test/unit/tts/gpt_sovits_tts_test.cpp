#include "module/tts/gpt_sovits_tts.h"

#include <gtest/gtest.h>
#include <httplib/httplib.h>

#include <algorithm>
#include <atomic>
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
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8u);
    }

    void WriteU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        for (size_t byte = 0; byte < 4; ++byte)
            bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8u));
    }

    std::vector<uint8_t> MakeWav()
    {
        std::vector<uint8_t> bytes(46, 0);
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
        WriteU32(bytes, 40, 2);
        return bytes;
    }

    class LocalTtsServer
    {
    public:
        explicit LocalTtsServer(std::function<void(const httplib::Request&, httplib::Response&)> handler)
        {
            server_.Post("/tts", std::move(handler));
            port_ = server_.bind_to_any_port("127.0.0.1");
            thread_ = std::thread([this] { server_.listen_after_bind(); });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!server_.is_running() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        ~LocalTtsServer()
        {
            server_.stop();
            if (thread_.joinable())
                thread_.join();
        }

        int Port() const { return port_; }
        bool IsRunning() const { return server_.is_running(); }

    private:
        httplib::Server server_;
        std::thread thread_;
        int port_ = 0;
    };

    kpengine::tts::ServerConfig MakeConfig(int port)
    {
        return {"127.0.0.1", static_cast<uint32_t>(port), "/tts", 5};
    }
}

TEST(GPTSovitsTtsTest, RejectsHttpErrorBeforePublishingBody)
{
    LocalTtsServer server([](const httplib::Request&, httplib::Response& response) {
        response.status = 503;
        response.set_content("{\"detail\":\"busy\"}", "application/json");
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    size_t delivered = 0;
    bool finished = false;
    std::string error;
    const bool success = provider.SynthesizeStream({}, {},
        [&](const uint8_t*, size_t) { ++delivered; return true; },
        [&] { finished = true; }, [&](const std::string& message) { error = message; },
        [] { return false; });

    EXPECT_FALSE(success);
    EXPECT_EQ(delivered, 0u);
    EXPECT_FALSE(finished);
    EXPECT_NE(error.find("HTTP 503"), std::string::npos);
}

TEST(GPTSovitsTtsTest, RejectsWrongMediaTypeBeforePublishingBody)
{
    LocalTtsServer server([](const httplib::Request&, httplib::Response& response) {
        response.set_content("not audio", "application/json");
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    size_t delivered = 0;
    bool finished = false;
    std::string error;
    const bool success = provider.SynthesizeStream({}, {},
        [&](const uint8_t*, size_t) { ++delivered; return true; },
        [&] { finished = true; }, [&](const std::string& message) { error = message; },
        [] { return false; });

    EXPECT_FALSE(success);
    EXPECT_EQ(delivered, 0u);
    EXPECT_FALSE(finished);
    EXPECT_NE(error.find("Content-Type"), std::string::npos);
}

TEST(GPTSovitsTtsTest, CompletesOnlyAfterDeliveringAValidResponse)
{
    const auto wav = MakeWav();
    LocalTtsServer server([&](const httplib::Request&, httplib::Response& response) {
        response.set_content(reinterpret_cast<const char*>(wav.data()), wav.size(), "audio/wav");
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    std::vector<uint8_t> delivered;
    bool finished = false;
    std::string error;
    const bool success = provider.SynthesizeStream({}, {},
        [&](const uint8_t* data, size_t size) {
            delivered.insert(delivered.end(), data, data + size);
            return true;
        }, [&] { finished = true; },
        [&](const std::string& message) { error = message; }, [] { return false; });

    EXPECT_TRUE(success) << error;
    EXPECT_EQ(delivered, wav);
    EXPECT_TRUE(finished);
}

TEST(GPTSovitsTtsTest, RejectsAnEmptyAudioBody)
{
    LocalTtsServer server([](const httplib::Request&, httplib::Response& response) {
        response.set_content("", "audio/wav");
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    size_t delivered = 0;
    bool finished = false;
    std::string error;
    const bool success = provider.SynthesizeStream({}, {},
        [&](const uint8_t*, size_t) { ++delivered; return true; },
        [&] { finished = true; }, [&](const std::string& message) { error = message; },
        [] { return false; });

    EXPECT_FALSE(success);
    EXPECT_EQ(delivered, 0u);
    EXPECT_FALSE(finished);
    EXPECT_NE(error.find("empty audio response"), std::string::npos);
}

TEST(GPTSovitsTtsTest, ConsumerRejectionAbortsWithoutFinish)
{
    const auto wav = MakeWav();
    LocalTtsServer server([&](const httplib::Request&, httplib::Response& response) {
        response.set_content(reinterpret_cast<const char*>(wav.data()), wav.size(), "audio/wav");
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    size_t delivered = 0;
    bool finished = false;
    std::string error;
    const bool success = provider.SynthesizeStream({}, {},
        [&](const uint8_t*, size_t) { ++delivered; return false; },
        [&] { finished = true; }, [&](const std::string& message) { error = message; },
        [] { return false; });

    EXPECT_FALSE(success);
    EXPECT_EQ(delivered, 1u);
    EXPECT_FALSE(finished);
    EXPECT_NE(error.find("consumer rejected"), std::string::npos);
}

TEST(GPTSovitsTtsTest, CancelStopsAnActiveHttpResponse)
{
    std::atomic<bool> response_started = false;
    std::atomic<bool> release_response = false;
    LocalTtsServer server([&](const httplib::Request&, httplib::Response& response) {
        response.set_chunked_content_provider("audio/wav",
            [&](size_t, httplib::DataSink& sink) {
                response_started.store(true, std::memory_order_release);
                while (!release_response.load(std::memory_order_acquire) && sink.is_writable())
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return true;
            });
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    const kpengine::tts::JobToken job{41};
    std::atomic<bool> finished = false;
    std::atomic<bool> success = true;
    std::atomic<bool> request_done = false;
    std::thread request([&] {
        const bool result = provider.SynthesizeStream(job, {},
            [](const uint8_t*, size_t) { return true; },
            [&] { finished.store(true, std::memory_order_release); },
            [](const std::string&) {}, [] { return false; });
        success.store(result, std::memory_order_release);
        request_done.store(true, std::memory_order_release);
    });

    const auto start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!response_started.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < start_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool started = response_started.load(std::memory_order_acquire);
    EXPECT_TRUE(started);
    provider.Cancel(job);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!request_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < cancel_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool aborted_promptly = request_done.load(std::memory_order_acquire);
    release_response.store(true, std::memory_order_release);
    request.join();

    EXPECT_TRUE(aborted_promptly);
    EXPECT_TRUE(request_done.load(std::memory_order_acquire));
    EXPECT_FALSE(success.load(std::memory_order_acquire));
    EXPECT_FALSE(finished.load(std::memory_order_acquire));
}

TEST(GPTSovitsTtsTest, DeliversBodyBeforeTheHttpResponseCompletes)
{
    const auto wav = MakeWav();
    std::mutex mutex;
    std::condition_variable cv;
    bool server_sent_first_chunk = false;
    bool release_server = false;
    LocalTtsServer server([&](const httplib::Request&, httplib::Response& response) {
        response.set_content_provider(wav.size(), "audio/wav",
            [&](size_t offset, size_t length, httplib::DataSink& sink) {
                const size_t first_chunk = std::min<size_t>(8, length);
                if (!sink.write(reinterpret_cast<const char*>(wav.data() + offset), first_chunk))
                    return false;
                {
                    std::unique_lock lock(mutex);
                    server_sent_first_chunk = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return release_server; });
                }
                return sink.write(reinterpret_cast<const char*>(wav.data() + offset + first_chunk),
                                  length - first_chunk);
            });
    });
    ASSERT_GT(server.Port(), 0);
    ASSERT_TRUE(server.IsRunning());

    kpengine::tts::GPTSovitsTTS provider;
    ASSERT_TRUE(provider.Initialize(MakeConfig(server.Port())));
    std::vector<uint8_t> delivered;
    std::mutex delivered_mutex;
    std::atomic<bool> callback_received = false;
    std::atomic<bool> finished = false;
    std::atomic<bool> success = false;
    std::atomic<bool> request_done = false;
    std::thread request([&] {
        const bool result = provider.SynthesizeStream({}, {},
            [&](const uint8_t* data, size_t size) {
                std::lock_guard lock(delivered_mutex);
                delivered.insert(delivered.end(), data, data + size);
                callback_received.store(true, std::memory_order_release);
                return true;
            }, [&] { finished.store(true, std::memory_order_release); },
            [](const std::string&) {}, [] { return false; });
        success.store(result, std::memory_order_release);
        request_done.store(true, std::memory_order_release);
    });

    bool first_chunk_arrived = false;
    {
        std::unique_lock lock(mutex);
        first_chunk_arrived = cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return server_sent_first_chunk;
        });
    }
    const auto client_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (first_chunk_arrived && !callback_received.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < client_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool delivered_while_open = callback_received.load(std::memory_order_acquire) &&
                                      !request_done.load(std::memory_order_acquire);
    {
        std::lock_guard lock(mutex);
        release_server = true;
        cv.notify_all();
    }
    request.join();

    EXPECT_TRUE(first_chunk_arrived);
    EXPECT_TRUE(delivered_while_open);
    EXPECT_TRUE(success.load(std::memory_order_acquire));
    EXPECT_TRUE(finished.load(std::memory_order_acquire));
    EXPECT_EQ(delivered, wav);
}
