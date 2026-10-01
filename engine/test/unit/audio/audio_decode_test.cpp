#include <gtest/gtest.h>
#include "runtime/asset/asset_manager.h"
#include "runtime/audio/audio_system.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/audio/stream_audio_player.h"
#include "runtime/audio/audio_stream.h"
#include "runtime/audio/audio_stream_decoder.h"
#include "runtime/audio/miniaudio_audio_system.h"

#include <cstdint>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace kpengine;

TEST(AudioDecodeTest, HelloWorld) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
    std::cout << "Hello from Audio Decode Test!" << std::endl;
}

TEST(AudioDecodeTest, LoopingPlayback) {
    auto &manager = asset::AssetManager::GetInstance();

    //  auto audio_path = std::string("D:\\dataset\\voice\\kurisu\\voice1\\voice1.wav");
    //  asset::AssetID id = manager.LoadSync(audio_path);
    //  auto resource = manager.GetResource<asset::AudioResource>(id);
    //  ASSERT_NE(resource, nullptr) << "Resource not found after load";
    // auto clip = resource->data;
    // auto duration = clip->GetDuration();
    
    // auto handle = audio_sys_->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    // audio::BufferAudioPlayer* player = 
    //     dynamic_cast<audio::BufferAudioPlayer*>(audio_sys_->GetAudioPlayer(handle));
    
    // player->SetShouldLoop(true);
    // player->SetClip(clip);
    // player->Play();
    
    // // 验证循环：播放超过一个完整周期
    // EXPECT_TRUE(player->IsPlaying());
    
    // // 等待一个完整循环 + 额外时间
    // std::this_thread::sleep_for(std::chrono::milliseconds(
    //     static_cast<int>(duration + 500)));
    
    // // 验证仍播放中（循环）
    // EXPECT_TRUE(player->IsPlaying());
    
    // // 验证位置已经重置（经历了循环）
    // auto current_pos = player->GetCurrentPosition();
    // EXPECT_LT(current_pos, duration); // 应该在循环内
    
    // player->Stop();
    // EXPECT_FALSE(player->IsPlaying());
}

namespace
{
    void AppendU16(std::vector<uint8_t>& bytes, uint16_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<uint8_t>(value >> shift));
    }

    std::vector<uint8_t> MakeMonoWav(const std::vector<int16_t>& samples,
                                     uint32_t sample_rate = 48000)
    {
        std::vector<uint8_t> bytes;
        bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
        AppendU32(bytes, static_cast<uint32_t>(36 + samples.size() * 2));
        bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        AppendU32(bytes, 16);
        AppendU16(bytes, 1);
        AppendU16(bytes, 1);
        AppendU32(bytes, sample_rate);
        AppendU32(bytes, sample_rate * 2);
        AppendU16(bytes, 2);
        AppendU16(bytes, 16);
        bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
        AppendU32(bytes, static_cast<uint32_t>(samples.size() * 2));
        for (int16_t sample : samples)
            AppendU16(bytes, static_cast<uint16_t>(sample));
        return bytes;
    }

    std::vector<uint8_t> MakeStereoWav(const std::vector<int16_t>& samples)
    {
        std::vector<uint8_t> bytes;
        bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
        AppendU32(bytes, static_cast<uint32_t>(36 + samples.size() * 2));
        bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        AppendU32(bytes, 16);
        AppendU16(bytes, 1);
        AppendU16(bytes, 2);
        AppendU32(bytes, 48000);
        AppendU32(bytes, 192000);
        AppendU16(bytes, 4);
        AppendU16(bytes, 16);
        bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
        AppendU32(bytes, static_cast<uint32_t>(samples.size() * 2));
        for (int16_t sample : samples)
            AppendU16(bytes, static_cast<uint16_t>(sample));
        return bytes;
    }
}

TEST(AudioStreamDecoderTest, AcceptsWavHeaderAndFramesSplitAtEveryByte)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 48000;
    auto stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder decoder(stream);
    const auto wav = MakeMonoWav({0, 16384, -16384});

    for (uint8_t byte : wav)
        EXPECT_NE(decoder.Feed(&byte, 1), audio::AudioDecodeResult::InvalidData);
    ASSERT_TRUE(decoder.Finish());

    float output[3]{};
    EXPECT_EQ(stream->ReadFrames(output, 3), 3u);
    EXPECT_FLOAT_EQ(output[0], 0.0f);
    EXPECT_FLOAT_EQ(output[1], 0.5f);
    EXPECT_FLOAT_EQ(output[2], -0.5f);
}

TEST(AudioStreamDecoderTest, AcceptsStreamingWavWithUnspecifiedDataLength)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 48000;
    auto stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder decoder(stream);
    auto wav = MakeMonoWav({0, 16384, -16384});
    wav[40] = wav[41] = wav[42] = wav[43] = 0;

    EXPECT_NE(decoder.Feed(wav.data(), wav.size()), audio::AudioDecodeResult::InvalidData);
    ASSERT_TRUE(decoder.Finish());

    float output[3]{};
    EXPECT_EQ(stream->ReadFrames(output, 3), 3u);
    EXPECT_FLOAT_EQ(output[0], 0.0f);
    EXPECT_FLOAT_EQ(output[1], 0.5f);
    EXPECT_FLOAT_EQ(output[2], -0.5f);
}

TEST(AudioStreamDecoderTest, KeepsStereoChannelFramesAlignedAcrossByteSplits)
{
    data::AudioFormat format{};
    format.channels = 2;
    format.sample_rate = 48000;
    auto stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder decoder(stream);
    const auto wav = MakeStereoWav({8192, -8192, 16384, -16384});

    for (uint8_t byte : wav)
        EXPECT_NE(decoder.Feed(&byte, 1), audio::AudioDecodeResult::InvalidData);
    ASSERT_TRUE(decoder.Finish());

    float output[4]{};
    EXPECT_EQ(stream->ReadFrames(output, 2), 2u);
    EXPECT_FLOAT_EQ(output[0], 0.25f);
    EXPECT_FLOAT_EQ(output[1], -0.25f);
    EXPECT_FLOAT_EQ(output[2], 0.5f);
    EXPECT_FLOAT_EQ(output[3], -0.5f);
}

TEST(AudioStreamDecoderTest, RejectsAnInvalidContainer)
{
    data::AudioFormat format{};
    auto stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder decoder(stream);
    const uint8_t invalid[] = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    EXPECT_EQ(decoder.Feed(invalid, sizeof(invalid)), audio::AudioDecodeResult::InvalidData);
    EXPECT_FALSE(decoder.Finish());
}

TEST(AudioStreamDecoderTest, ResamplingKeepsItsPhaseAcrossChunkBoundaries)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 48000;
    auto whole_stream = std::make_shared<audio::AudioStream>(format, 1);
    auto split_stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder whole_decoder(whole_stream);
    audio::AudioStreamDecoder split_decoder(split_stream);
    const auto wav = MakeMonoWav({0, 16384, 32767}, 24000);

    EXPECT_NE(whole_decoder.Feed(wav.data(), wav.size()), audio::AudioDecodeResult::InvalidData);
    ASSERT_TRUE(whole_decoder.Finish());
    const size_t chunks[] = {1, 7, 3, 2, 9, 1, 4, 5, 2};
    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < wav.size())
    {
        const size_t count = std::min(chunks[chunk_index++ % std::size(chunks)], wav.size() - offset);
        EXPECT_NE(split_decoder.Feed(wav.data() + offset, count), audio::AudioDecodeResult::InvalidData);
        offset += count;
    }
    ASSERT_TRUE(split_decoder.Finish());

    float whole[5]{};
    float split[5]{};
    ASSERT_EQ(whole_stream->ReadFrames(whole, 5), 5u);
    ASSERT_EQ(split_stream->ReadFrames(split, 5), 5u);
    for (size_t i = 0; i < 5; ++i)
        EXPECT_NEAR(split[i], whole[i], 1e-6f);
    EXPECT_FLOAT_EQ(split[0], 0.0f);
    EXPECT_NEAR(split[1], 0.25f, 1e-4f);
    EXPECT_NEAR(split[2], 0.5f, 1e-4f);
    EXPECT_NEAR(split[3], 0.75f, 1e-4f);
    EXPECT_NEAR(split[4], 32767.0f / 32768.0f, 1e-4f);
}

TEST(AudioStreamDecoderTest, RejectsTruncatedAndOverstatedDataChunks)
{
    data::AudioFormat format{};
    format.channels = 1;
    auto truncated_stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder truncated_decoder(truncated_stream);
    auto truncated = MakeMonoWav({0, 16384, -16384});
    truncated.resize(truncated.size() - 2);
    EXPECT_NE(truncated_decoder.Feed(truncated.data(), truncated.size()),
              audio::AudioDecodeResult::InvalidData);
    EXPECT_FALSE(truncated_decoder.Finish());

    auto overstated_stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder overstated_decoder(overstated_stream);
    auto overstated = MakeMonoWav({0, 16384, -16384});
    overstated[40] = 8; // The response contains only six data bytes.
    overstated[41] = overstated[42] = overstated[43] = 0;
    EXPECT_NE(overstated_decoder.Feed(overstated.data(), overstated.size()),
              audio::AudioDecodeResult::InvalidData);
    EXPECT_FALSE(overstated_decoder.Finish());

    auto short_stream = std::make_shared<audio::AudioStream>(format, 1);
    audio::AudioStreamDecoder short_decoder(short_stream);
    auto short_data = MakeMonoWav({0, 16384, -16384});
    short_data[40] = 4; // Declared length ends before all received samples.
    short_data[41] = short_data[42] = short_data[43] = 0;
    EXPECT_EQ(short_decoder.Feed(short_data.data(), short_data.size()),
              audio::AudioDecodeResult::InvalidData);
}

TEST(AudioStreamTest, RejectsOverflowWithoutDroppingQueuedSpeech)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 2;
    audio::AudioStream stream(format, 1);
    const float first[] = {0.25f, 0.5f};
    const float overflow[] = {0.75f};
    ASSERT_TRUE(stream.PushFrames(first, 2));
    EXPECT_FALSE(stream.PushFrames(overflow, 1));

    float output[2]{};
    EXPECT_EQ(stream.ReadFrames(output, 2), 2u);
    EXPECT_FLOAT_EQ(output[0], first[0]);
    EXPECT_FLOAT_EQ(output[1], first[1]);
}

TEST(AudioStreamTest, FinishedStreamRejectsLateProducerWrites)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 2;
    audio::AudioStream stream(format, 1);
    const float sample = 0.5f;
    stream.Finish();
    EXPECT_FALSE(stream.PushFrames(&sample, 1));
    EXPECT_TRUE(stream.IsFinished());
}

TEST(AudioStreamTest, ProducerWaitsForSpaceWithoutDroppingFrames)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 2;
    audio::AudioStream stream(format, 1);
    const float first[] = {0.25f, 0.5f};
    const float next = 0.75f;
    ASSERT_TRUE(stream.PushFrames(first, 2));

    std::atomic<bool> pushed = false;
    std::thread producer([&] {
        pushed.store(stream.PushFramesWait(&next, 1), std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(pushed.load(std::memory_order_acquire));

    float output[2]{};
    ASSERT_EQ(stream.ReadFrames(output, 1), 1u);
    EXPECT_FLOAT_EQ(output[0], first[0]);
    producer.join();
    ASSERT_TRUE(pushed.load(std::memory_order_acquire));

    ASSERT_EQ(stream.ReadFrames(output, 2), 2u);
    EXPECT_FLOAT_EQ(output[0], first[1]);
    EXPECT_FLOAT_EQ(output[1], next);
}

TEST(AudioStreamTest, WaitingProducerCanBeCancelled)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 2;
    audio::AudioStream stream(format, 1);
    const float first[] = {0.25f, 0.5f};
    const float next = 0.75f;
    ASSERT_TRUE(stream.PushFrames(first, 2));
    std::atomic<bool> cancelled = false;
    bool pushed = true;
    std::thread producer([&] {
        pushed = stream.PushFramesWait(&next, 1, [&] {
            return cancelled.load(std::memory_order_acquire);
        });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    cancelled.store(true, std::memory_order_release);
    producer.join();
    EXPECT_FALSE(pushed);
    EXPECT_EQ(stream.GetOverflowRejectionCount(), 0u);
}

TEST(AudioStreamTest, SeparatesProducerFinishFromConsumerDrain)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 48000;
    audio::AudioStream stream(format, 1);
    const float samples[] = {0.1f, 0.2f, 0.3f};
    ASSERT_TRUE(stream.PushFrames(samples, 3));
    stream.Finish();
    EXPECT_EQ(stream.GetState(), audio::AudioStreamState::ProducerFinished);

    float output[3]{};
    ASSERT_EQ(stream.ReadFrames(output, 3), 3u);
    EXPECT_EQ(stream.GetState(), audio::AudioStreamState::Drained);
    for (size_t i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(output[i], samples[i]);
}

TEST(AudioStreamTest, ReportsOverflowAndCancellationExplicitly)
{
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 2;
    audio::AudioStream stream(format, 1);
    const float samples[] = {0.25f, -0.25f};
    ASSERT_TRUE(stream.PushFrames(samples, 2));
    EXPECT_FALSE(stream.PushFrames(samples, 1));
    EXPECT_EQ(stream.GetOverflowRejectionCount(), 1u);
    EXPECT_EQ(stream.GetState(), audio::AudioStreamState::Open);

    stream.Cancel();
    EXPECT_EQ(stream.GetState(), audio::AudioStreamState::Cancelled);
    float output[2]{};
    EXPECT_EQ(stream.ReadFrames(output, 2), 0u);
    EXPECT_FALSE(stream.PushFrames(samples, 1));
}

TEST(AudioMixerTest, UpmixesMonoAndClearsTheWholeOutputBlock)
{
    audio::MiniAudioSystem system;
    const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(handle));
    ASSERT_NE(player, nullptr);

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 300;
    clip->pcm.resize(300);
    for (size_t frame = 0; frame < clip->pcm.size(); ++frame)
        clip->pcm[frame] = frame % 2 == 0 ? 0.25f : -0.5f;
    player->SetClip(clip);
    player->Play();

    std::vector<float> output(300 * 2, 9.f);
    system.Mix(output.data(), 300);
    EXPECT_NEAR(output[299 * 2], -0.5f, 1e-5f);
    EXPECT_NEAR(output[299 * 2 + 1], -0.5f, 1e-5f);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 300u);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Finished);
}

TEST(AudioMixerTest, StereoGainPauseSeekLoopAndEndAreFrameSafe)
{
    audio::MiniAudioSystem system;
    const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(handle));
    ASSERT_NE(player, nullptr);

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 2;
    clip->format.sample_rate = 48000;
    clip->frame_count = 1024;
    clip->pcm.reserve(clip->frame_count * 2);
    for (uint32_t frame = 0; frame < clip->frame_count; ++frame)
    {
        clip->pcm.push_back(0.25f);
        clip->pcm.push_back(-0.5f);
    }
    player->SetClip(clip);
    player->SetVolume(0.5f);
    player->Play();

    std::array<float, 512 * 2> output{};
    system.Mix(output.data(), 512);
    EXPECT_NEAR(output[400 * 2], 0.125f, 1e-5f);
    EXPECT_NEAR(output[400 * 2 + 1], -0.25f, 1e-5f);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 512u);

    player->Pause();
    system.Mix(output.data(), 240);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Paused);
    const uint64_t paused_cursor = player->GetPlayedFrameCursor();
    system.Mix(output.data(), 32);
    EXPECT_EQ(player->GetPlayedFrameCursor(), paused_cursor);
    EXPECT_FLOAT_EQ(output[0], 0.0f);

    ASSERT_TRUE(player->SeekFrames(5));
    system.Mix(output.data(), 1);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 5u);
    player->SetShouldLoop(true);
    player->Play();
    system.Mix(output.data(), 1);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 6u);
    ASSERT_TRUE(player->SeekFrames(1022));
    system.Mix(output.data(), 1);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 1023u);
    system.Mix(output.data(), 4);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 3u);

    player->SetShouldLoop(false);
    ASSERT_TRUE(player->SeekFrames(1022));
    system.Mix(output.data(), 1);
    ASSERT_EQ(player->GetPlayedFrameCursor(), 1023u);
    system.Mix(output.data(), 8);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 1024u);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Finished);
    for (size_t sample = 2; sample < 8 * 2; ++sample)
        EXPECT_FLOAT_EQ(output[sample], 0.0f);
}

TEST(AudioMixerTest, SpeechMusicAndMasterGainsRampIndependently)
{
    audio::MiniAudioSystem system;
    const auto speech_handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    const auto music_handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto speech = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(speech_handle));
    auto music = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(music_handle));
    ASSERT_NE(speech, nullptr);
    ASSERT_NE(music, nullptr);

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 512;
    clip->pcm.assign(clip->frame_count, 0.4f);
    speech->SetClip(clip);
    music->SetClip(clip);
    speech->SetBus(audio::AudioBus::Speech);
    music->SetBus(audio::AudioBus::Music);
    music->Play();
    speech->Play();
    system.SetBusGain(audio::AudioBus::Speech, 0.5f);
    system.SetBusMuted(audio::AudioBus::Music, true);
    system.SetMasterGain(0.5f);

    std::array<float, 300 * 2> output{};
    system.Mix(output.data(), 300);
    EXPECT_NEAR(output[299 * 2], 0.1f, 1e-5f);
    EXPECT_NEAR(output[299 * 2 + 1], 0.1f, 1e-5f);
    EXPECT_EQ(speech->GetPlayedFrameCursor(), 300u);
    EXPECT_EQ(music->GetPlayedFrameCursor(), 300u);
    EXPECT_TRUE(system.IsBusMuted(audio::AudioBus::Music));
    EXPECT_FLOAT_EQ(system.GetBusGain(audio::AudioBus::Speech), 0.5f);
    EXPECT_FLOAT_EQ(system.GetMasterGain(), 0.5f);
}

TEST(AudioMixerTest, DryStreamBuffersThenDrainsEveryFrameOnce)
{
    audio::MiniAudioSystem system;
    const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Stream);
    auto player = std::dynamic_pointer_cast<audio::StreamAudioPlayer>(system.GetAudioPlayer(handle));
    ASSERT_NE(player, nullptr);
    data::AudioFormat format{};
    format.channels = 1;
    format.sample_rate = 48000;
    auto stream = std::make_shared<audio::AudioStream>(format, 1);
    player->SetStream(stream);
    player->Play();

    std::array<float, 64 * 2> output{};
    output.fill(1.0f);
    system.Mix(output.data(), 64);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Buffering);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 0u);
    for (float sample : output)
        EXPECT_FLOAT_EQ(sample, 0.0f);

    std::array<float, 128> frames{};
    frames.fill(0.25f);
    ASSERT_TRUE(stream->PushFrames(frames.data(), frames.size()));
    stream->Finish();
    system.Mix(output.data(), 64);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 64u);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Playing);
    system.Mix(output.data(), 64);
    EXPECT_EQ(stream->GetState(), audio::AudioStreamState::Drained);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 128u);
    EXPECT_EQ(player->GetCurrentState(), audio::AudioState::Finished);
    system.Mix(output.data(), 64);
    EXPECT_EQ(player->GetPlayedFrameCursor(), 128u);

    const auto telemetry = system.GetTelemetrySnapshot();
    EXPECT_GE(telemetry.buses[static_cast<size_t>(audio::AudioBus::Speech)].underrun_blocks, 1u);
    EXPECT_EQ(telemetry.buses[static_cast<size_t>(audio::AudioBus::Speech)].played_frames, 128u);
}

TEST(AudioSystemTest, RetiresHandlesAndPublishesPlayerSnapshotsDuringMix)
{
    audio::MiniAudioSystem system;
    auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    ASSERT_NE(system.GetAudioPlayer(handle), nullptr);
    ASSERT_TRUE(system.DestroyAudioPlayer(handle));
    EXPECT_EQ(system.GetAudioPlayer(handle), nullptr);

    const auto reused = system.CreateAudioPlayer(audio::AudioPlayerType::Stream);
    EXPECT_EQ(reused.id, handle.id);
    EXPECT_NE(reused.generation, handle.generation);
    EXPECT_NE(system.GetAudioPlayer(reused), nullptr);

    std::thread mixer([&system]
    {
        float output[128]{};
        for (int i = 0; i < 500; ++i)
            system.Mix(output, 64);
    });
    for (int i = 0; i < 500; ++i)
    {
        const auto transient = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
        EXPECT_TRUE(system.DestroyAudioPlayer(transient));
    }
    mixer.join();
}

TEST(AudioSystemTest, LimitsActiveVoicesToTheMixerBudget)
{
    audio::MiniAudioSystem system;
    std::vector<audio::AudioHandle> handles;
    for (int i = 0; i < 64; ++i)
    {
        auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
        ASSERT_TRUE(handle.IsValid());
        handles.push_back(handle);
    }
    EXPECT_FALSE(system.CreateAudioPlayer(audio::AudioPlayerType::Buffer).IsValid());
    for (const auto handle : handles)
        EXPECT_TRUE(system.DestroyAudioPlayer(handle));
}

TEST(AudioSystemTest, AppliesConfiguredVoiceAndCallbackBudgets)
{
    audio::AudioSystemSettings settings{};
    settings.max_voices = 2;
    settings.max_callback_frames = 64;
    settings.gain_ramp_frames = 0;
    settings.device_period_count = 2;
    audio::MiniAudioSystem system(settings);
    EXPECT_EQ(system.GetSettings().max_voices, 2u);
    EXPECT_EQ(system.GetSettings().max_callback_frames, 64u);
    EXPECT_EQ(system.GetSettings().device_period_count, 2u);

    const auto first_handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    const auto second_handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    EXPECT_TRUE(first_handle.IsValid());
    EXPECT_TRUE(second_handle.IsValid());
    EXPECT_FALSE(system.CreateAudioPlayer(audio::AudioPlayerType::Buffer).IsValid());
    auto first = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(first_handle));
    ASSERT_NE(first, nullptr);

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 128;
    clip->pcm.assign(clip->frame_count, 0.25f);
    first->SetClip(clip);
    first->Play();

    std::array<float, 100 * 2> output{};
    output.fill(9.0f);
    system.Mix(output.data(), 100);
    EXPECT_EQ(first->GetPlayedFrameCursor(), 64u);
    EXPECT_FLOAT_EQ(output[63 * 2], 0.25f);
    EXPECT_FLOAT_EQ(output[64 * 2], 0.0f);
    EXPECT_FLOAT_EQ(output.back(), 0.0f);
    EXPECT_EQ(system.GetTelemetrySnapshot().callback_work_limited_frames, 36u);

    EXPECT_TRUE(system.DestroyAudioPlayer(first_handle));
    EXPECT_TRUE(system.DestroyAudioPlayer(second_handle));
}

TEST(AudioSystemTest, ReportsPlaybackCommandQueueRejections)
{
    audio::MiniAudioSystem system;
    const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto player = system.GetAudioPlayer(handle);
    ASSERT_NE(player, nullptr);
    for (uint32_t command = 0; command < 17; ++command)
        player->SetShouldLoop((command & 1u) != 0);

    std::array<float, 2> output{};
    system.Mix(output.data(), 1);
    EXPECT_EQ(system.GetTelemetrySnapshot().control_command_rejections, 1u);
    EXPECT_TRUE(system.DestroyAudioPlayer(handle));
}

TEST(AudioSystemTest, ConcurrentCommandsCreateDestroyAndMixStayBounded)
{
    audio::MiniAudioSystem system;
    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 2048;
    clip->pcm.assign(clip->frame_count, 0.05f);

    std::atomic<bool> running{true};
    std::atomic<uint32_t> created{0};
    std::thread mixer([&]
    {
        std::array<float, 128> output{};
        while (running.load(std::memory_order_acquire))
            system.Mix(output.data(), 64);
    });

    std::array<std::thread, 4> controllers;
    for (uint32_t thread_index = 0; thread_index < controllers.size(); ++thread_index)
    {
        controllers[thread_index] = std::thread([&, thread_index]
        {
            for (uint32_t iteration = 0; iteration < 500; ++iteration)
            {
                const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
                if (!handle.IsValid())
                    continue;
                ++created;
                auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(
                    system.GetAudioPlayer(handle));
                if (player)
                {
                    player->SetClip(clip);
                    player->SetBus((thread_index & 1u) == 0 ? audio::AudioBus::Speech
                                                            : audio::AudioBus::Music);
                    player->SetVolume((iteration % 10) / 10.0f);
                    player->SetShouldLoop((iteration & 1u) != 0);
                    player->Play();
                    if ((iteration % 3) == 0)
                        player->Pause();
                    player->Restart();
                }
                EXPECT_TRUE(system.DestroyAudioPlayer(handle));
                EXPECT_EQ(system.GetAudioPlayer(handle), nullptr);
            }
        });
    }
    for (auto& controller : controllers)
        controller.join();
    running.store(false, std::memory_order_release);
    mixer.join();

    EXPECT_GT(created.load(), 0u);
    const auto telemetry = system.GetTelemetrySnapshot();
    EXPECT_GT(telemetry.callback_count, 0u);
    EXPECT_GT(telemetry.callback_frames, 0u);
    EXPECT_GT(telemetry.max_callback_duration_ns, 0u);
    EXPECT_EQ(telemetry.concurrent_callback_rejections, 0u);
}

TEST(AudioMixerTest, MeasuresWorstCaseVoiceMixDuration)
{
    audio::MiniAudioSystem system;
    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 2;
    clip->format.sample_rate = 48000;
    clip->frame_count = 4096;
    clip->pcm.assign(clip->frame_count * 2, 0.001f);

    std::vector<audio::AudioHandle> handles;
    for (uint32_t voice_index = 0; voice_index < audio::AudioSystem::kMaxVoices; ++voice_index)
    {
        const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
        ASSERT_TRUE(handle.IsValid());
        handles.push_back(handle);
        auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(handle));
        ASSERT_NE(player, nullptr);
        player->SetClip(clip);
        player->Play();
    }

    std::array<float, audio::AudioSystem::kMaxCallbackFrames * 2> output{};
    for (uint32_t iteration = 0; iteration < 200; ++iteration)
        system.Mix(output.data(), audio::AudioSystem::kMaxCallbackFrames);
    const auto telemetry = system.GetTelemetrySnapshot();
    EXPECT_EQ(telemetry.callback_count, 200u);
    EXPECT_EQ(telemetry.callback_frames, 200u * audio::AudioSystem::kMaxCallbackFrames);
    EXPECT_GT(telemetry.max_callback_duration_ns, 0u);
    EXPECT_LT(telemetry.max_callback_duration_ns, 100'000'000u);
    std::cout << "64 voices x 512 frames max callback: "
              << telemetry.max_callback_duration_ns << " ns\n";

    for (const auto handle : handles)
        EXPECT_TRUE(system.DestroyAudioPlayer(handle));
}
