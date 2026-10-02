#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "asset/audio_importer.h"
#include "asset/asset_import_adapters.h"
#include "asset/asset_import_registry.h"

namespace
{
    void WriteU16(std::ofstream &output, std::uint16_t value)
    {
        output.put(static_cast<char>(value));
        output.put(static_cast<char>(value >> 8));
    }

    void WriteU32(std::ofstream &output, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            output.put(static_cast<char>(value >> shift));
    }

    void WriteU64(std::vector<std::byte> &bytes, std::size_t offset, std::uint64_t value)
    {
        for (std::size_t index = 0; index < 8; ++index)
            bytes[offset + index] = static_cast<std::byte>(value >> (index * 8));
    }

    std::uint32_t ReadU32(const std::vector<std::byte> &bytes, std::size_t offset)
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index)
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                     << (index * 8);
        return value;
    }

    std::uint64_t ReadU64(const std::vector<std::byte> &bytes, std::size_t offset)
    {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index)
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                     << (index * 8);
        return value;
    }

    void RehashNativeAudio(std::vector<std::byte> &bytes)
    {
        const auto digest = kpengine::asset::Sha256WithZeroedRange(
            bytes, kpengine::asset::kNativeAudioDigestOffset,
            kpengine::asset::kNativeAudioDigestSize);
        ASSERT_TRUE(digest.has_value());
        for (std::size_t index = 0; index < digest->zeroed_range_hash.bytes.size(); ++index)
            bytes[kpengine::asset::kNativeAudioDigestOffset + index] =
                static_cast<std::byte>(digest->zeroed_range_hash.bytes[index]);
    }

    void WriteWave(const std::filesystem::path &path, std::uint32_t frames)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        const std::uint32_t data_size = frames * 4;
        output.write("RIFF", 4);
        WriteU32(output, 36 + data_size);
        output.write("WAVEfmt ", 8);
        WriteU32(output, 16);
        WriteU16(output, 1);
        WriteU16(output, 2);
        WriteU32(output, 48000);
        WriteU32(output, 192000);
        WriteU16(output, 4);
        WriteU16(output, 16);
        output.write("data", 4);
        WriteU32(output, data_size);
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            const auto sample = static_cast<std::int16_t>(
                std::sin(static_cast<double>(frame) * 0.04) * 20'000.0);
            WriteU16(output, static_cast<std::uint16_t>(sample));
            WriteU16(output, static_cast<std::uint16_t>(sample));
        }
        ASSERT_TRUE(output.good());
    }

    void WriteText(const std::filesystem::path &path, std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(output.good());
    }

    struct TempRoot
    {
        TempRoot()
        {
            static std::atomic_uint64_t sequence{};
            path = std::filesystem::temp_directory_path() /
                ("kpengine_audio_import_test_" +
                 std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::filesystem::create_directories(path / "music");
        }
        ~TempRoot()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        std::filesystem::path path;
    };

    kpengine::asset::NativeAudioData MakeProductData()
    {
        kpengine::asset::NativeAudioData data{};
        data.codec = kpengine::asset::NativeAudioCodec::Wav;
        data.duration_frames = 48'000;
        data.encoded_audio = {std::byte{0x52}, std::byte{0x49}, std::byte{0x46}, std::byte{0x46}};
        data.waveform.resize(kpengine::asset::kNativeAudioWaveformBucketCount,
                             {-0.5f, 0.5f});
        return data;
    }
}

TEST(AudioImportTest, ParsesSrtAndWebVttIntoTheNormalizedPlayedFrameTimeline)
{
    TempRoot root;
    const auto audio = root.path / "music" / "tone.wav";
    const auto srt = root.path / "music" / "tone.srt";
    const auto vtt = root.path / "music" / "tone.vtt";
    WriteWave(audio, 48'000);
    WriteText(srt, "1\r\n00:00:00,000 --> 00:00:00,500\r\nHello\r\n\r\n"
                   "2\r\n00:00:00,500 --> 00:00:01,000\r\n世界\r\n");
    WriteText(vtt, "WEBVTT\n\n00:00.000 --> 00:00.500 align:start\nFirst\n\n"
                   "00:00.500 --> 00:01.000\nSecond\n");

    kpengine::asset::AudioImportRequest request{};
    request.asset_root = root.path;
    request.source_path = "music/tone.wav";
    request.options = {"music/tone.srt", "en"};
    auto srt_product = kpengine::asset::AudioImporter{}.Import(request);
    ASSERT_EQ(srt_product.data.subtitles.size(), 2u);
    EXPECT_EQ(srt_product.data.subtitles[0].start_frame, 0u);
    EXPECT_EQ(srt_product.data.subtitles[0].end_frame, 24'000u);
    EXPECT_EQ(srt_product.data.subtitles[1].start_frame, 24'000u);
    EXPECT_EQ(srt_product.data.subtitles[1].end_frame, 48'000u);
    EXPECT_EQ(srt_product.data.subtitles[1].text, "世界");
    EXPECT_EQ(srt_product.data.subtitle_language, "en");
    EXPECT_EQ(srt_product.data.waveform.size(), kpengine::asset::kNativeAudioWaveformBucketCount);
    EXPECT_EQ(srt_product.data.duration_frames, 48'000u);

    request.options = {"music/tone.vtt", "und"};
    auto vtt_product = kpengine::asset::AudioImporter{}.Import(request);
    ASSERT_EQ(vtt_product.data.subtitles.size(), 2u);
    EXPECT_EQ(vtt_product.data.subtitles.front().text, "First");
    EXPECT_EQ(vtt_product.data.subtitles.back().text, "Second");

    const auto decoded = kpengine::asset::DeserializeNativeAudio(srt_product.bytes);
    EXPECT_EQ(decoded.data.subtitles, srt_product.data.subtitles);
    EXPECT_EQ(decoded.product_hash, srt_product.product_hash);
    EXPECT_EQ(kpengine::asset::AudioImporter{}.Import(request).bytes, vtt_product.bytes);
}

TEST(AudioImportTest, ParsesLrcOffsetsAndSameTimeTranslations)
{
    TempRoot root;
    WriteWave(root.path / "music" / "tone.wav", 48'000);
    WriteText(root.path / "music" / "tone.lrc",
              "[ti:Test Song]\n[offset:+100]\n[00:00.00][00:00.50]Translated line\n"
              "[00:00.50]同時刻の翻訳\n");
    kpengine::asset::AudioImportRequest request{};
    request.asset_root = root.path;
    request.source_path = "music/tone.wav";
    request.options = {"music/tone.lrc", "ja"};
    const auto product = kpengine::asset::AudioImporter{}.Import(request);
    ASSERT_EQ(product.data.subtitles.size(), 3u);
    EXPECT_EQ(product.data.subtitles[0].start_frame, 4'800u);
    EXPECT_EQ(product.data.subtitles[0].end_frame, 28'800u);
    EXPECT_EQ(product.data.subtitles[1].start_frame, 28'800u);
    EXPECT_EQ(product.data.subtitles[1].end_frame, 48'000u);
    EXPECT_EQ(product.data.subtitles[2].start_frame, 28'800u);
    EXPECT_EQ(product.data.subtitles[2].end_frame, 48'000u);
    EXPECT_EQ(product.data.subtitles[2].text, "同時刻の翻訳");
}

TEST(AudioImportTest, MissingSubtitleIsValidButExplicitMalformedSubtitleFailsBeforePublish)
{
    TempRoot root;
    const auto audio = root.path / "music" / "tone.wav";
    WriteWave(audio, 48'000);
    kpengine::asset::AudioImportRequest request{};
    request.asset_root = root.path;
    request.source_path = "music/tone.wav";
    const auto without_text = kpengine::asset::AudioImporter{}.Import(request);
    EXPECT_TRUE(without_text.data.subtitles.empty());
    EXPECT_TRUE(without_text.data.subtitle_language.empty());

    request.source_path = "../outside.wav";
    EXPECT_THROW(kpengine::asset::AudioImporter{}.Import(request),
                 kpengine::asset::AudioImportError);
    request.source_path = "music/tone.wav";

    const auto bad_subtitle = root.path / "music" / "bad.srt";
    WriteText(bad_subtitle, "1\n00:00:00,000 --> 00:00:02,000\nOutside duration\n");
    request.options = {"music/bad.srt", "en"};
    const auto archive = root.path / ".archive";
    EXPECT_THROW(kpengine::asset::AudioImporter{}.Import(request),
                 kpengine::asset::AudioImportError);
    EXPECT_FALSE(std::filesystem::exists(archive / "audio"));
}

TEST(AudioImportTest, NativeProductRejectsBadDirectoryAndRoundTripsDeterministically)
{
    const auto data = MakeProductData();
    const auto first = kpengine::asset::SerializeNativeAudio(data);
    const auto second = kpengine::asset::SerializeNativeAudio(data);
    EXPECT_EQ(first, second);
    EXPECT_NO_THROW(kpengine::asset::ValidateNativeAudioProductStructure(first));

    auto malformed = first;
    constexpr std::size_t kFirstChunkOffsetField =
        kpengine::asset::kNativeAudioHeaderSize + 8;
    for (std::size_t index = 0; index < 8; ++index)
        malformed[kFirstChunkOffsetField + index] =
            static_cast<std::byte>(kpengine::asset::kNativeAudioHeaderSize >> (index * 8));
    RehashNativeAudio(malformed);
    EXPECT_THROW(kpengine::asset::ValidateNativeAudioProductStructure(malformed),
                 kpengine::asset::NativeAudioError);

    auto invalid_range_data = data;
    invalid_range_data.subtitle_language = "en";
    invalid_range_data.subtitles.push_back({0, 24'000, "valid cue"});
    auto invalid_range_product = kpengine::asset::SerializeNativeAudio(invalid_range_data);
    for (std::size_t index = 0; index < ReadU32(invalid_range_product, 32); ++index)
    {
        const std::size_t entry = kpengine::asset::kNativeAudioHeaderSize +
                                  index * kpengine::asset::kNativeAudioDirectoryEntrySize;
        if (ReadU32(invalid_range_product, entry) != 4) continue;
        const std::size_t subtitle_payload = static_cast<std::size_t>(ReadU64(invalid_range_product, entry + 8));
        WriteU64(invalid_range_product, subtitle_payload + 16, 48'001);
        break;
    }
    RehashNativeAudio(invalid_range_product);
    EXPECT_THROW(kpengine::asset::ValidateNativeAudioProductStructure(invalid_range_product),
                 kpengine::asset::NativeAudioError);

    auto invalid_cue = data;
    invalid_cue.subtitles.push_back({48'000, 24'000, "bad range"});
    invalid_cue.subtitle_language = "en";
    EXPECT_THROW(kpengine::asset::SerializeNativeAudio(invalid_cue),
                 kpengine::asset::NativeAudioError);
}

TEST(AudioImportTest, AudioProviderPublishesOnlyAValidatedProduct)
{
    TempRoot root;
    const auto audio = root.path / "music" / "tone.wav";
    const auto subtitle = root.path / "music" / "tone.srt";
    WriteWave(audio, 48'000);
    WriteText(subtitle, "1\n00:00:00,000 --> 00:00:00,250\nValid\n");
    kpengine::asset::ImportProviderRegistry registry{};
    std::string diagnostic;
    ASSERT_TRUE(kpengine::asset::RegisterAudioImportProvider(registry, diagnostic)) << diagnostic;
    ASSERT_TRUE(registry.Seal(diagnostic)) << diagnostic;
    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root.path;
    request.archive_root = root.path / ".archive";
    request.source_path = "music/tone.wav";
    request.audio_options = kpengine::asset::AudioImportOptions{"music/tone.srt", "en"};
    const auto result = registry.Execute(request, "audio", diagnostic);
    ASSERT_NE(result.product, nullptr) << diagnostic;
    const auto cooked = std::dynamic_pointer_cast<kpengine::asset::TypedImportProduct<
        kpengine::asset::CookedAudio, kpengine::asset::ImportProviderKind::Audio>>(result.product);
    ASSERT_NE(cooked, nullptr);
    const auto product_path = request.archive_root / "audio" /
        (cooked->value.product_hash.ToHex() + ".audio");
    ASSERT_TRUE(std::filesystem::is_regular_file(product_path));
    const auto stored_size = std::filesystem::file_size(product_path);
    EXPECT_EQ(stored_size, cooked->value.bytes.size());

    WriteText(subtitle, "not a subtitle cue\n");
    request.archive_root = root.path / "failed-import" / ".archive";
    EXPECT_THROW(registry.Execute(request, "audio", diagnostic), kpengine::asset::AudioImportError);
    EXPECT_FALSE(std::filesystem::exists(request.archive_root / "audio"));
}
