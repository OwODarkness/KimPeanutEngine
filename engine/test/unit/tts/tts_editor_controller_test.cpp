#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "module/tts/editor/tts_editor_controller.h"

namespace
{
    using namespace std::chrono_literals;

    void WriteU16(std::vector<std::uint8_t> &bytes, std::size_t offset,
                  std::uint16_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
                  std::uint32_t value)
    {
        for (std::size_t index = 0; index < 4; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8));
    }

    std::vector<std::uint8_t> MakeWav()
    {
        constexpr std::uint32_t frames = 480;
        std::vector<std::uint8_t> bytes(44 + frames * 2, 0);
        std::memcpy(bytes.data(), "RIFF", 4);
        WriteU32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
        std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
        WriteU32(bytes, 16, 16);
        WriteU16(bytes, 20, 1);
        WriteU16(bytes, 22, 1);
        WriteU32(bytes, 24, 48000);
        WriteU32(bytes, 28, 96000);
        WriteU16(bytes, 32, 2);
        WriteU16(bytes, 34, 16);
        std::memcpy(bytes.data() + 36, "data", 4);
        WriteU32(bytes, 40, frames * 2);
        return bytes;
    }

    class OfflineAudioSystem final : public kpengine::audio::AudioSystem
    {
    public:
        bool Initialize() override { initialized_ = true; return true; }
        void ShutDown() override { initialized_ = false; }
        bool IsInitialized() const override { return initialized_; }

    private:
        bool initialized_ = false;
    };

    struct ProviderState
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool hold_started = false;
        bool released = false;
        std::vector<kpengine::tts::ServerConfig> servers;
        std::vector<kpengine::tts::TTSRequest> requests;
    };

    class EditorFakeProvider final : public kpengine::tts::ITTSProvider
    {
    public:
        explicit EditorFakeProvider(std::shared_ptr<ProviderState> state)
            : state_(std::move(state)) {}

        bool Initialize(const kpengine::tts::ServerConfig &config) override
        {
            std::lock_guard lock(state_->mutex);
            state_->servers.push_back(config);
            return true;
        }
        void ShutDown() override { Release(); }
        void Cancel(kpengine::tts::JobToken) override { Release(); }

        bool SynthesizeBuffer(kpengine::tts::JobToken,
            const kpengine::tts::TTSRequest &request,
            kpengine::tts::AudioDataCallback on_data,
            kpengine::tts::ErrorCallback on_error,
            kpengine::tts::CancellationCheck is_cancelled) override
        {
            {
                std::lock_guard lock(state_->mutex);
                state_->requests.push_back(request);
            }
            if (request.text == "hold" && !WaitForCancel(is_cancelled)) return false;
            if (request.text == "fail")
            {
                on_error("Synthetic provider failure");
                return false;
            }
            const auto wav = MakeWav();
            return !is_cancelled() && on_data(wav.data(), wav.size());
        }

        bool SynthesizeStream(kpengine::tts::JobToken,
            const kpengine::tts::TTSRequest &request,
            kpengine::tts::AudioDataCallback on_data,
            kpengine::tts::FinishCallback on_finish,
            kpengine::tts::ErrorCallback on_error,
            kpengine::tts::CancellationCheck is_cancelled) override
        {
            {
                std::lock_guard lock(state_->mutex);
                state_->requests.push_back(request);
            }
            if (request.text == "hold" && !WaitForCancel(is_cancelled)) return false;
            if (request.text == "fail")
            {
                on_error("Synthetic provider failure");
                return false;
            }
            const auto wav = MakeWav();
            if (is_cancelled() || !on_data(wav.data(), wav.size())) return false;
            on_finish();
            return true;
        }

    private:
        void Release()
        {
            std::lock_guard lock(state_->mutex);
            state_->released = true;
            state_->cv.notify_all();
        }

        bool WaitForCancel(const kpengine::tts::CancellationCheck &is_cancelled)
        {
            std::unique_lock lock(state_->mutex);
            state_->hold_started = true;
            state_->cv.notify_all();
            state_->cv.wait_for(lock, 2s, [&] {
                return state_->released || is_cancelled();
            });
            return !is_cancelled();
        }

        std::shared_ptr<ProviderState> state_;
    };

    struct ControllerFixture
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kp-tts-controller-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::path settings_path = directory / "settings.json";

        ControllerFixture()
        {
            kpengine::tts_editor::TtsEditorSettings settings;
            settings.address = "test-server";
            settings.port = 1234;
            settings.voices.front().ref_audio_path = "server/voice.wav";
            settings.voices.front().ref_text = "reference";
            settings.voices.front().ref_language = "en";
            settings.text_language = "en";
            std::string diagnostic;
            EXPECT_TRUE(kpengine::tts_editor::SaveSettings(settings_path, settings, diagnostic))
                << diagnostic;
        }

        ~ControllerFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    };

    template<typename Predicate>
    bool Until(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(2ms);
        }
        return predicate();
    }
}

TEST(TtsEditorControllerTest, SettingsSwitchChangesOnlyNewJobsAndTheirPreviewMode)
{
    ControllerFixture fixture;
    auto audio = std::make_unique<OfflineAudioSystem>();
    OfflineAudioSystem *const mixer = audio.get();
    auto state = std::make_shared<ProviderState>();
    kpengine::tts_editor::TtsEditorController controller(
        fixture.settings_path, std::move(audio),
        [state] { return std::make_unique<EditorFakeProvider>(state); });
    std::string diagnostic;
    ASSERT_TRUE(controller.Initialize(diagnostic));

    controller.QueueGenerate("first");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        std::array<float, 1024> output{};
        mixer->Mix(output.data(), 512);
        controller.Tick();
        const auto view = controller.GetView();
        return view.entries.size() == 1 && view.entries[0].player.IsValid();
    }));

    auto updated = controller.GetView().settings;
    updated.streaming = false;
    updated.voices.push_back({"voice-2", "Second voice", "server/second.wav",
                              "second reference", "zh"});
    updated.selected_voice_id = "voice-2";
    controller.QueueSettings(updated);
    controller.Tick();
    controller.QueueGenerate("second");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        std::array<float, 1024> output{};
        mixer->Mix(output.data(), 512);
        controller.Tick();
        const auto view = controller.GetView();
        return view.entries.size() == 2 && view.entries[1].player.IsValid();
    }));
    const auto view = controller.GetView();
    EXPECT_TRUE(view.entries[0].streaming);
    EXPECT_FALSE(view.entries[1].streaming);
    EXPECT_EQ(view.entries[1].voice_name, "Second voice");
    EXPECT_TRUE(view.duration_seconds.has_value());
    {
        std::lock_guard lock(state->mutex);
        ASSERT_EQ(state->requests.size(), 2u);
        EXPECT_EQ(state->requests[0].ref_audio_path, "server/voice.wav");
        EXPECT_EQ(state->requests[1].ref_audio_path, "server/second.wav");
        EXPECT_EQ(state->requests[1].prompt_text, "second reference");
        EXPECT_FALSE(state->requests[1].streaming);
    }
    controller.Shutdown();
}

TEST(TtsEditorControllerTest, CancellingActiveJobRejectsLateAudioAndAllowsNextJob)
{
    ControllerFixture fixture;
    auto state = std::make_shared<ProviderState>();
    kpengine::tts_editor::TtsEditorController controller(
        fixture.settings_path, std::make_unique<OfflineAudioSystem>(),
        [state] { return std::make_unique<EditorFakeProvider>(state); });
    std::string diagnostic;
    ASSERT_TRUE(controller.Initialize(diagnostic));
    controller.QueueGenerate("hold");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(state->mutex);
        return state->hold_started;
    }));
    controller.QueueCancel();
    controller.Tick();
    controller.QueueGenerate("next");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        controller.Tick();
        const auto view = controller.GetView();
        return view.entries.size() == 2 && view.entries[1].player.IsValid();
    }));
    const auto view = controller.GetView();
    EXPECT_EQ(view.entries[0].state, kpengine::tts::TTSJobState::Cancelled);
    EXPECT_FALSE(view.entries[0].player.IsValid());
    EXPECT_TRUE(view.entries[1].player.IsValid());
    controller.Shutdown();
}

TEST(TtsEditorControllerTest, ProviderFailureIsVisibleAndShutdownJoinsActiveWork)
{
    ControllerFixture fixture;
    auto state = std::make_shared<ProviderState>();
    kpengine::tts_editor::TtsEditorController controller(
        fixture.settings_path, std::make_unique<OfflineAudioSystem>(),
        [state] { return std::make_unique<EditorFakeProvider>(state); });
    std::string diagnostic;
    ASSERT_TRUE(controller.Initialize(diagnostic));
    controller.QueueGenerate("fail");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        controller.Tick();
        const auto view = controller.GetView();
        return view.entries.size() == 1 &&
            view.entries[0].state == kpengine::tts::TTSJobState::Failed;
    }));
    const auto failed = controller.GetView();
    EXPECT_EQ(failed.entries[0].error, "Synthetic provider failure");
    EXPECT_FALSE(failed.entries[0].player.IsValid());

    controller.QueueGenerate("hold");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(state->mutex);
        return state->hold_started;
    }));
    controller.Shutdown();
    controller.Tick();
    EXPECT_TRUE(controller.GetView().entries.empty());
}

TEST(TtsEditorControllerTest, ConnectionEditCancelsOldJobAndReinitializesProvider)
{
    ControllerFixture fixture;
    auto state = std::make_shared<ProviderState>();
    kpengine::tts_editor::TtsEditorController controller(
        fixture.settings_path, std::make_unique<OfflineAudioSystem>(),
        [state] { return std::make_unique<EditorFakeProvider>(state); });
    std::string diagnostic;
    ASSERT_TRUE(controller.Initialize(diagnostic));
    controller.QueueGenerate("hold");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        std::lock_guard lock(state->mutex);
        return state->hold_started;
    }));

    auto updated = controller.GetView().settings;
    updated.address = "another-server";
    updated.port = 4321;
    controller.QueueSettings(updated);
    controller.Tick();
    controller.QueueGenerate("after-edit");
    controller.Tick();
    ASSERT_TRUE(Until([&] {
        controller.Tick();
        const auto view = controller.GetView();
        return view.entries.size() == 2 && view.entries[1].player.IsValid();
    }));
    const auto view = controller.GetView();
    EXPECT_EQ(view.entries[0].state, kpengine::tts::TTSJobState::Cancelled);
    EXPECT_FALSE(view.entries[0].player.IsValid());
    {
        std::lock_guard lock(state->mutex);
        ASSERT_EQ(state->servers.size(), 2u);
        EXPECT_EQ(state->servers[0].host, "test-server");
        EXPECT_EQ(state->servers[1].host, "another-server");
        EXPECT_EQ(state->servers[1].port, 4321);
    }
    controller.Shutdown();
}

TEST(TtsEditorControllerTest, CompletedSpeechPersistsAndRestoresAsBufferPreview)
{
    ControllerFixture fixture;
    auto audio = std::make_unique<OfflineAudioSystem>();
    OfflineAudioSystem *const mixer = audio.get();
    auto state = std::make_shared<ProviderState>();
    {
        kpengine::tts_editor::TtsEditorController controller(
            fixture.settings_path, std::move(audio),
            [state] { return std::make_unique<EditorFakeProvider>(state); });
        std::string diagnostic;
        ASSERT_TRUE(controller.Initialize(diagnostic));
        controller.QueueGenerate("persist me");
        controller.Tick();
        ASSERT_TRUE(Until([&] {
            std::array<float, 1024> output{};
            mixer->Mix(output.data(), 512);
            controller.Tick();
            const auto view = controller.GetView();
            return view.entries.size() == 1 && view.entries[0].durable;
        }));
        EXPECT_EQ(controller.GetView().entries[0].state,
                  kpengine::tts::TTSJobState::Completed);
        controller.Shutdown();
    }

    kpengine::tts_editor::TtsEditorController restored(
        fixture.settings_path, std::make_unique<OfflineAudioSystem>(), {});
    std::string diagnostic;
    ASSERT_TRUE(restored.Initialize(diagnostic));
    restored.Tick();
    auto view = restored.GetView();
    ASSERT_EQ(view.entries.size(), 1u);
    EXPECT_TRUE(view.entries[0].durable);
    EXPECT_FALSE(view.entries[0].streaming);
    restored.QueueTogglePlayPause();
    restored.Tick();
    view = restored.GetView();
    EXPECT_TRUE(view.entries[0].player.IsValid());
    EXPECT_TRUE(view.duration_seconds.has_value());
    restored.Shutdown();
}

TEST(TtsEditorControllerTest, MalformedLibraryRemainsUntouchedByQueuedMutations)
{
    ControllerFixture fixture;
    const auto manifest = fixture.directory / "save" / "tts_editor" / "library.json";
    std::filesystem::create_directories(manifest.parent_path());
    { std::ofstream stream(manifest, std::ios::binary); stream << "{ repair me"; }
    kpengine::tts_editor::TtsEditorController controller(
        fixture.settings_path, std::make_unique<OfflineAudioSystem>(), {});
    std::string diagnostic;
    ASSERT_TRUE(controller.Initialize(diagnostic));
    EXPECT_FALSE(controller.GetView().library_loaded);
    controller.QueueImport("missing.wav");
    controller.Tick();
    std::ifstream stream(manifest, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), {});
    EXPECT_EQ(contents, "{ repair me");
    EXPECT_FALSE(controller.GetView().error.empty());
    controller.Shutdown();
}
