#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "module/tts/editor/tts_editor_library.h"
#include "module/tts/editor/tts_editor_wav.h"

namespace
{
    using namespace kpengine::tts_editor;

    void Write16(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint16_t value)
    {
        bytes[at] = static_cast<std::uint8_t>(value);
        bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void Write32(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
            bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }

    std::vector<std::uint8_t> MakeWav()
    {
        std::vector<std::uint8_t> wav(48, 0);
        std::memcpy(wav.data(), "RIFF", 4);
        Write32(wav, 4, 40);
        std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
        Write32(wav, 16, 16);
        Write16(wav, 20, 1);
        Write16(wav, 22, 1);
        Write32(wav, 24, 24000);
        Write32(wav, 28, 48000);
        Write16(wav, 32, 2);
        Write16(wav, 34, 16);
        std::memcpy(wav.data() + 36, "data", 4);
        Write32(wav, 40, 4);
        wav[44] = 0x21; wav[45] = 0x43; wav[46] = 0x65; wav[47] = 0x07;
        return wav;
    }

    struct TempDirectory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("kp-tts-library-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TempDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    };
}

TEST(TtsEditorWavTest, CanonicalizesUnknownStreamingLengthsAndRejectsMalformedInput)
{
    auto input = MakeWav();
    Write32(input, 4, 0);
    Write32(input, 40, 0);
    std::vector<std::uint8_t> output;
    std::string diagnostic;
    ASSERT_TRUE(CanonicalizeWav(input, output, diagnostic)) << diagnostic;
    EXPECT_EQ(output.size(), input.size());
    EXPECT_EQ(output[4], 40);
    EXPECT_EQ(output[40], 4);

    input[20] = 3;
    EXPECT_FALSE(CanonicalizeWav(input, output, diagnostic));
    EXPECT_FALSE(diagnostic.empty());
}

TEST(TtsEditorWavTest, AcceptsTheThirtyTwoMiBBoundAndRejectsOverflow)
{
    std::vector<std::uint8_t> wav(kMaximumWavBytes, 0);
    std::memcpy(wav.data(), "RIFF", 4);
    Write32(wav, 4, static_cast<std::uint32_t>(wav.size() - 8));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    Write32(wav, 16, 16);
    Write16(wav, 20, 1);
    Write16(wav, 22, 1);
    Write32(wav, 24, 48000);
    Write32(wav, 28, 96000);
    Write16(wav, 32, 2);
    Write16(wav, 34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    Write32(wav, 40, static_cast<std::uint32_t>(wav.size() - 44));
    std::vector<std::uint8_t> output;
    std::string diagnostic;
    ASSERT_TRUE(CanonicalizeWav(wav, output, diagnostic)) << diagnostic;
    wav.push_back(0);
    EXPECT_FALSE(CanonicalizeWav(wav, output, diagnostic));
}

TEST(TtsEditorLibraryTest, PersistsSharedArtifactsAndExportsCollisionSafeFiles)
{
    TempDirectory temp;
    TtsEditorLibrary library(temp.path / "library");
    std::string artifact, diagnostic;
    const auto wav = MakeWav();
    ASSERT_TRUE(library.Store(1, wav, artifact, diagnostic)) << diagnostic;
    ASSERT_TRUE(library.Store(2, wav, artifact, diagnostic)) << diagnostic;
    std::vector<StoredDialog> rows{
        {1, "hello", "voice", true, artifact},
        {2, "hello", "voice", true, artifact}};
    ASSERT_TRUE(library.Save(rows, 3, diagnostic)) << diagnostic;

    std::vector<StoredDialog> loaded;
    std::uint64_t next_id = 0;
    ASSERT_TRUE(library.Load(loaded, next_id, diagnostic)) << diagnostic;
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(next_id, 3u);
    EXPECT_EQ(loaded[0].artifact, loaded[1].artifact);

    std::filesystem::create_directories(temp.path / "out");
    const auto prior = temp.path / "out" / "speech.wav";
    { std::ofstream stream(prior, std::ios::binary); stream << "keep"; }
    std::filesystem::path first, second;
    ASSERT_TRUE(library.Export(loaded[0].artifact, temp.path / "out", "speech", first, diagnostic)) << diagnostic;
    ASSERT_TRUE(library.Export(loaded[1].artifact, temp.path / "out", "speech", second, diagnostic)) << diagnostic;
    EXPECT_EQ(first.filename(), "speech-2.wav");
    EXPECT_EQ(second.filename(), "speech-3.wav");
    std::ifstream prior_file(prior, std::ios::binary);
    const std::string prior_contents((std::istreambuf_iterator<char>(prior_file)), {});
    EXPECT_EQ(prior_contents, "keep");
    std::ifstream exported(first, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(exported)), {});
    EXPECT_EQ(bytes.size(), wav.size());

    std::vector<StoredDialog> after_delete{loaded.back()};
    ASSERT_TRUE(library.Save(after_delete, 3, diagnostic)) << diagnostic;
    EXPECT_TRUE(std::filesystem::exists(temp.path / "library" / "audio" / loaded[0].artifact));
    std::vector<std::uint8_t> restored;
    ASSERT_TRUE(library.Read(loaded[0].artifact, restored, diagnostic)) << diagnostic;
}

TEST(TtsEditorLibraryTest, InvalidManifestIsRetainedAndUnsafeExportIsRejected)
{
    TempDirectory temp;
    const auto root = temp.path / "library";
    std::filesystem::create_directories(root);
    const auto manifest = root / "library.json";
    { std::ofstream stream(manifest); stream << "{ broken"; }
    TtsEditorLibrary library(root);
    std::vector<StoredDialog> rows;
    std::uint64_t next_id = 0;
    std::string diagnostic;
    EXPECT_FALSE(library.Load(rows, next_id, diagnostic));
    EXPECT_TRUE(std::filesystem::exists(manifest));
    std::filesystem::path published;
    EXPECT_FALSE(library.Export("../outside.wav", temp.path, "x", published, diagnostic));
    const auto bad_wav = std::vector<std::uint8_t>{'n', 'o', 't', ' ', 'w', 'a', 'v'};
    std::string artifact;
    EXPECT_FALSE(library.Store(1, bad_wav, artifact, diagnostic));

    const auto blocked_path = temp.path / "file";
    { std::ofstream stream(blocked_path); stream << "file"; }
    const auto good_root = temp.path / "valid-library";
    TtsEditorLibrary valid_library(good_root);
    const auto wav = MakeWav();
    ASSERT_TRUE(valid_library.Store(1, wav, artifact, diagnostic)) << diagnostic;
    EXPECT_FALSE(valid_library.Export(artifact, blocked_path / "child", "speech", published, diagnostic));
}
