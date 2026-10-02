#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "database/database.h"
#include "asset/audio_importer.h"
#include "asset/audio_import_service.h"
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
        kpengine::asset::AudioImportSummary,
        kpengine::asset::ImportProviderKind::Audio>>(result.product);
    ASSERT_NE(cooked, nullptr);
    const auto product_path = cooked->value.product_path;
    ASSERT_TRUE(std::filesystem::is_regular_file(product_path));
    const auto stored_size = std::filesystem::file_size(product_path);
    EXPECT_EQ(cooked->value.duration_frames, 48'000u);
    EXPECT_EQ(cooked->value.waveform_buckets,
              kpengine::asset::kNativeAudioWaveformBucketCount);
    EXPECT_EQ(cooked->value.subtitle_cues, 1u);
    EXPECT_FALSE(cooked->value.up_to_date);

    WriteText(subtitle, "not a subtitle cue\n");
    request.archive_root = root.path / "failed-import" / ".archive";
    EXPECT_THROW(registry.Execute(request, "audio", diagnostic), kpengine::asset::AudioImportError);
    EXPECT_FALSE(std::filesystem::exists(request.archive_root / "audio"));
}

TEST(AudioImportTest, AudioImportNoOpsAndReimportReuseReplaceAndClearRecordedSubtitle)
{
    TempRoot root;
    WriteWave(root.path / "music" / "tone.wav", 48'000);
    const auto subtitle_path = root.path / "music" / "tone.srt";
    WriteText(subtitle_path, "1\n00:00:00,000 --> 00:00:00,250\nFirst\n");

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root.path;
    request.archive_root = root.path / ".archive";
    request.source_path = "music/tone.wav";
    request.audio_options = kpengine::asset::AudioImportOptions{"music/tone.srt", "en"};
    kpengine::asset::AudioImportService service{};
    const auto first = service.Import(request);
    ASSERT_FALSE(first.up_to_date);
    ASSERT_TRUE(std::filesystem::exists(first.product_path));
    const auto first_time = std::filesystem::last_write_time(first.product_path);

    request.reimport = true;
    request.audio_options.reset();
    const auto no_op = service.Import(request);
    EXPECT_TRUE(no_op.up_to_date);
    EXPECT_EQ(no_op.product_hash, first.product_hash);
    EXPECT_EQ(std::filesystem::last_write_time(no_op.product_path), first_time);
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);

    WriteText(subtitle_path, "1\n00:00:00,000 --> 00:00:00,250\nUpdated\n");
    const auto replaced = service.Import(request);
    EXPECT_FALSE(replaced.up_to_date);
    EXPECT_NE(replaced.product_hash, first.product_hash);
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);

    request.audio_options = kpengine::asset::AudioImportOptions{"music/tone.srt", "ja"};
    const auto settings_changed = service.Import(request);
    EXPECT_FALSE(settings_changed.up_to_date);
    EXPECT_NE(settings_changed.product_hash, replaced.product_hash);
    request.audio_options.reset();
    request.clear_subtitle = true;
    const auto cleared = service.Import(request);
    EXPECT_FALSE(cleared.up_to_date);
    EXPECT_NE(cleared.product_hash, replaced.product_hash);
    const auto snapshot = kpengine::asset::ModelArchiveDatabase{
        request.archive_root / "archive.sqlite3"}.FindSource("music/tone.wav");
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(snapshot->source.audio_metadata.has_value());
    EXPECT_TRUE(snapshot->source.audio_metadata->subtitle_path.empty());
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);
    kpengine::asset::ModelArchiveDatabase archive{request.archive_root / "archive.sqlite3"};
    EXPECT_NO_THROW(archive.IntegrityCheck());
    const auto catalog = archive.ReadCatalog();
    ASSERT_EQ(catalog.products.size(), 4u);
    EXPECT_TRUE(std::all_of(catalog.products.begin(), catalog.products.end(),
        [](const auto &product)
        {
            return product.asset_type == kpengine::asset::ArchiveProductType::Audio;
        }));
    ASSERT_EQ(catalog.sources.size(), 1u);
    EXPECT_TRUE(catalog.sources.front().source.audio_metadata.has_value());

    std::vector<char> corrupted;
    {
        std::ifstream input(cleared.product_path, std::ios::binary);
        corrupted.assign(std::istreambuf_iterator<char>{input}, {});
    }
    ASSERT_FALSE(corrupted.empty());
    corrupted.back() ^= 1;
    {
        std::ofstream output(cleared.product_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(corrupted.data(), static_cast<std::streamsize>(corrupted.size()));
    }
    EXPECT_EQ(service.Status(request).status,
              kpengine::asset::ArchiveProbeStatus::CorruptProduct);
    const auto repaired = service.Import(request);
    EXPECT_FALSE(repaired.up_to_date);
    EXPECT_EQ(repaired.product_hash, cleared.product_hash);
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);

    std::filesystem::remove(repaired.product_path);
    EXPECT_EQ(service.Status(request).status,
              kpengine::asset::ArchiveProbeStatus::MissingProduct);
    const auto restored = service.Import(request);
    EXPECT_FALSE(restored.up_to_date);
    EXPECT_EQ(restored.product_hash, repaired.product_hash);

    WriteWave(root.path / "music" / "tone.wav", 48'001);
    const auto source_changed = service.Import(request);
    EXPECT_FALSE(source_changed.up_to_date);
    EXPECT_NE(source_changed.product_hash, restored.product_hash);
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);
    std::filesystem::remove(root.path / "music" / "tone.wav");
    EXPECT_EQ(service.Status(request).status,
              kpengine::asset::ArchiveProbeStatus::SourceInputMissing);
}

TEST(AudioImportTest, FailedReimportPreservesReadyAssociationAndStatusFindsMissingSubtitle)
{
    TempRoot root;
    WriteWave(root.path / "music" / "tone.wav", 48'000);
    const auto subtitle_path = root.path / "music" / "tone.srt";
    WriteText(subtitle_path, "1\n00:00:00,000 --> 00:00:00,250\nGood\n");

    kpengine::asset::ImportProviderRequest request{};
    request.asset_root = root.path;
    request.archive_root = root.path / ".archive";
    request.source_path = "music/tone.wav";
    request.audio_options = kpengine::asset::AudioImportOptions{"music/tone.srt", "en"};
    kpengine::asset::AudioImportService service{};
    const auto ready = service.Import(request);
    request.reimport = true;

    WriteText(subtitle_path, "invalid subtitle input\n");
    EXPECT_THROW(service.Import(request), kpengine::asset::AudioImportError);
    const auto after_failure = kpengine::asset::ModelArchiveDatabase{
        request.archive_root / "archive.sqlite3"}.FindSource("music/tone.wav");
    ASSERT_TRUE(after_failure.has_value());
    EXPECT_EQ(after_failure->source_products.front().content_hash, ready.product_hash);
    EXPECT_EQ(service.Status(request).status,
              kpengine::asset::ArchiveProbeStatus::SourcePackageChanged);

    WriteText(subtitle_path, "1\n00:00:00,000 --> 00:00:00,250\nGood\n");
    EXPECT_EQ(service.Status(request).status, kpengine::asset::ArchiveProbeStatus::UpToDate);

    WriteText(subtitle_path, "1\n00:00:00,000 --> 00:00:00,250\nCommit failure\n");
    {
        kpengine::database::Database raw{
            (request.archive_root / "archive.sqlite3").string()};
        raw.Execute(
            "CREATE TRIGGER reject_audio_source_update BEFORE UPDATE ON sources "
            "WHEN OLD.normalized_path='music/tone.wav' BEGIN "
            "SELECT RAISE(ABORT, 'injected archive commit failure'); END;");
    }
    EXPECT_THROW(service.Import(request), kpengine::asset::ModelArchiveError);
    const auto after_commit_failure = kpengine::asset::ModelArchiveDatabase{
        request.archive_root / "archive.sqlite3"}.FindSource("music/tone.wav");
    ASSERT_TRUE(after_commit_failure.has_value());
    EXPECT_EQ(after_commit_failure->source_products.front().content_hash, ready.product_hash);
    EXPECT_EQ(service.Status(request).status,
              kpengine::asset::ArchiveProbeStatus::SourcePackageChanged);
    {
        kpengine::database::Database raw{
            (request.archive_root / "archive.sqlite3").string()};
        raw.Execute("DROP TRIGGER reject_audio_source_update;");
    }

    std::filesystem::remove(subtitle_path);
    const auto status = service.Status(request);
    EXPECT_EQ(status.status, kpengine::asset::ArchiveProbeStatus::DependencyMissing);
}
