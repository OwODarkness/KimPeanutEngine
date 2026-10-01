#include <gtest/gtest.h>
#include "runtime/asset/asset_manager.h"
#include "runtime/audio/audio_system.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/audio/audio_stream.h"
#include "runtime/audio/audio_stream_decoder.h"
#include "runtime/audio/miniaudio_audio_system.h"

#include <cstdint>
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

TEST(AudioMixerTest, UpmixesMonoAndClearsTheWholeOutputBlock)
{
    audio::MiniAudioSystem system;
    const auto handle = system.CreateAudioPlayer(audio::AudioPlayerType::Buffer);
    auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(system.GetAudioPlayer(handle));
    ASSERT_NE(player, nullptr);

    auto clip = std::make_shared<data::AudioClip>();
    clip->format.channels = 1;
    clip->format.sample_rate = 48000;
    clip->frame_count = 2;
    clip->pcm = {0.25f, -0.5f};
    player->SetClip(clip);
    player->Play();

    float output[] = {9.f, 9.f, 9.f, 9.f};
    system.Mix(output, 2);
    EXPECT_FLOAT_EQ(output[0], 0.25f);
    EXPECT_FLOAT_EQ(output[1], 0.25f);
    EXPECT_FLOAT_EQ(output[2], -0.5f);
    EXPECT_FLOAT_EQ(output[3], -0.5f);
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
