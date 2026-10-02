#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset/native_audio.h"
#include "audio/audio_system.h"
#include "audio/seekable_audio_player.h"

namespace
{
    using namespace std::chrono_literals;
    constexpr std::uint32_t kFixtureDurationFrames = 48000u * 60u;

    void AppendU16(std::vector<std::byte> &bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xffu));
        bytes.push_back(static_cast<std::byte>(value >> 8));
    }

    void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }

    std::vector<std::byte> MakeWave(const std::uint32_t frames)
    {
        const std::uint32_t data_bytes = frames * 4;
        std::vector<std::byte> bytes;
        bytes.reserve(44 + data_bytes);
        for (const char value : std::string_view{"RIFF"}) bytes.push_back(static_cast<std::byte>(value));
        AppendU32(bytes, 36 + data_bytes);
        for (const char value : std::string_view{"WAVEfmt "}) bytes.push_back(static_cast<std::byte>(value));
        AppendU32(bytes, 16);
        AppendU16(bytes, 1);
        AppendU16(bytes, 2);
        AppendU32(bytes, 48000);
        AppendU32(bytes, 48000 * 4);
        AppendU16(bytes, 4);
        AppendU16(bytes, 16);
        for (const char value : std::string_view{"data"}) bytes.push_back(static_cast<std::byte>(value));
        AppendU32(bytes, data_bytes);
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            AppendU16(bytes, static_cast<std::uint16_t>(12000));
            AppendU16(bytes, static_cast<std::uint16_t>(12000));
        }
        return bytes;
    }

    struct AudioFixture
    {
        std::filesystem::path directory;
        std::shared_ptr<const kpengine::asset::NativeAudioFileProduct> product;

        AudioFixture()
        {
            directory = std::filesystem::temp_directory_path() /
                ("kp_seekable_audio_" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(directory);
            kpengine::asset::NativeAudioData data{};
            data.codec = kpengine::asset::NativeAudioCodec::Wav;
            data.channels = 2;
            data.sample_rate = 48000;
            data.duration_frames = kFixtureDurationFrames;
            data.encoded_audio = MakeWave(kFixtureDurationFrames);
            data.waveform.assign(kpengine::asset::kNativeAudioWaveformBucketCount,
                                 {-0.4f, 0.4f});
            data.subtitle_language = "en";
            data.subtitles = {{0, 12000, "opening"}, {0, 12000, "translation"},
                              {12000, 24000, "next"}};
            const auto bytes = kpengine::asset::SerializeNativeAudio(data);
            const auto path = directory / "fixture.audio";
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.close();
            product = std::make_shared<const kpengine::asset::NativeAudioFileProduct>(
                kpengine::asset::ReadNativeAudioFile(path));
        }

        ~AudioFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

        kpengine::audio::FileBackedAudioSource Source() const
        {
            return {product->path, product->encoded_audio_offset,
                    product->encoded_audio_size, product->metadata.duration_frames, product};
        }
    };

    class OfflineAudioSystem final : public kpengine::audio::AudioSystem
    {
    public:
        using AudioSystem::AudioSystem;
        bool Initialize() override { return true; }
        void ShutDown() override {}
    };
}

TEST(SeekableAudioPlayerTest, NativeAudioFileLoaderKeepsEncodedPayloadFileBacked)
{
    AudioFixture fixture;
    ASSERT_TRUE(fixture.product);
    EXPECT_TRUE(fixture.product->metadata.encoded_audio.empty());
    EXPECT_EQ(fixture.product->encoded_audio_size, 44u +
              static_cast<std::uint64_t>(kFixtureDurationFrames) * 4u);
    EXPECT_EQ(fixture.product->metadata.waveform.size(),
              kpengine::asset::kNativeAudioWaveformBucketCount);
    const auto at_start = fixture.product->SubtitleTextAt(0);
    ASSERT_EQ(at_start.size(), 2u);
    EXPECT_EQ(at_start[0], "opening");
    EXPECT_EQ(at_start[1], "translation");
    EXPECT_TRUE(fixture.product->SubtitleTextAt(12000).front() == "next");
    EXPECT_TRUE(fixture.product->SubtitleTextAt(24000).empty());
}

TEST(SeekableAudioPlayerTest, StreamsThroughBoundedCacheAndUsesPlayedFrameForSeekRateAndLoop)
{
    AudioFixture fixture;
    OfflineAudioSystem system;
    const auto handle = system.CreateAudioPlayer(kpengine::audio::AudioPlayerType::Seekable);
    ASSERT_TRUE(handle.IsValid());
    auto player = std::dynamic_pointer_cast<kpengine::audio::SeekableAudioPlayer>(
        system.GetAudioPlayer(handle));
    ASSERT_TRUE(player);
    ASSERT_TRUE(player->SetSource(fixture.Source())) << player->GetDiagnostic();
    EXPECT_LT(player->GetFixedBufferBytes(), 8u * 1024u * 1024u);
    EXPECT_GT(player->GetBufferedFrameCount(), 0u);
    EXPECT_LT(player->GetBufferedFrameCount(), kFixtureDurationFrames);
    EXPECT_EQ(player->GetAudioFormat().channels, 2u);
    EXPECT_EQ(player->GetAudioFormat().sample_rate, 48000u);

    player->SetBus(kpengine::audio::AudioBus::Music);
    player->Play();
    std::vector<float> output(1024 * 2);
    const auto finish_pause = [&]
    {
        player->Pause();
        for (int attempt = 0; attempt < 100 &&
             player->GetCurrentState() != kpengine::audio::AudioState::Paused; ++attempt)
        {
            system.Mix(output.data(), 240);
            if (player->GetCurrentState() != kpengine::audio::AudioState::Paused)
                std::this_thread::sleep_for(1ms);
        }
    };
    system.Mix(output.data(), 1024);
    EXPECT_TRUE(std::any_of(output.begin(), output.end(),
                            [](const float sample) { return std::abs(sample) > 0.01f; }));
    EXPECT_GT(player->GetPlayedFrameCursor(), 0u);

    finish_pause();
    ASSERT_EQ(player->GetCurrentState(), kpengine::audio::AudioState::Paused);
    const std::uint64_t paused_frame = player->GetPlayedFrameCursor();
    system.Mix(output.data(), 512);
    EXPECT_EQ(player->GetPlayedFrameCursor(), paused_frame);

    ASSERT_TRUE(player->SeekFrames(24000));
    system.Mix(output.data(), 1);
    ASSERT_EQ(player->GetPlayedFrameCursor(), 24000u);
    bool seek_ready = false;
    for (int attempt = 0; attempt < 250 && !seek_ready; ++attempt)
    {
        std::uint32_t channels = 0;
        seek_ready = player->CopyFrames(24000, output.data(), 1, channels) == 1;
        if (!seek_ready) std::this_thread::sleep_for(2ms);
    }
    ASSERT_TRUE(seek_ready);
    ASSERT_TRUE(player->SetPlaybackRate(2.0f));
    player->Play();
    system.Mix(output.data(), 128);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 24256u);

    finish_pause();
    player->SetShouldLoop(true);
    constexpr std::uint64_t loop_start = kFixtureDurationFrames - 16;
    ASSERT_TRUE(player->SeekFrames(loop_start));
    system.Mix(output.data(), 1);
    ASSERT_EQ(player->GetPlayedFrameCursor(), loop_start);
    ASSERT_TRUE(player->SetPlaybackRate(1.0f));
    seek_ready = false;
    for (int attempt = 0; attempt < 250 && !seek_ready; ++attempt)
    {
        std::uint32_t channels = 0;
        seek_ready = player->CopyFrames(loop_start, output.data(), 16, channels) == 16;
        if (!seek_ready) std::this_thread::sleep_for(2ms);
    }
    ASSERT_TRUE(seek_ready);
    player->Play();
    for (int attempt = 0; attempt < 100 && player->GetPlayedFrameCursor() >= 64; ++attempt)
    {
        system.Mix(output.data(), 32);
        if (player->GetPlayedFrameCursor() >= 64) std::this_thread::sleep_for(1ms);
    }
    EXPECT_LT(player->GetPlayedFrameCursor(), 64u);

    ASSERT_TRUE(system.DestroyAudioPlayer(handle));
    player.reset();
}

TEST(SeekableAudioPlayerTest, RetainsTheNativeProductUntilTheVoiceIsDestroyed)
{
    AudioFixture fixture;
    std::weak_ptr<const kpengine::asset::NativeAudioFileProduct> weak = fixture.product;
    {
        kpengine::audio::SeekableAudioPlayer player;
        ASSERT_TRUE(player.SetSource(fixture.Source())) << player.GetDiagnostic();
        fixture.product.reset();
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}
