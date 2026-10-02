#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_AUDIO_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_AUDIO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "asset_product.h"

namespace kpengine::asset
{
    constexpr std::uint16_t kNativeAudioVersion = 1;
    constexpr std::size_t kNativeAudioHeaderSize = 72;
    constexpr std::size_t kNativeAudioDirectoryEntrySize = 32;
    constexpr std::size_t kNativeAudioDigestOffset = 40;
    constexpr std::size_t kNativeAudioDigestSize = kModelArchiveHashSize;
    constexpr std::uint64_t kNativeAudioMaxSourceBytes = 512ull * 1024ull * 1024ull;
    constexpr std::uint64_t kNativeAudioMaxProductBytes =
        kNativeAudioMaxSourceBytes + 16ull * 1024ull * 1024ull;
    constexpr std::uint32_t kNativeAudioWaveformBucketCount = 384;
    constexpr std::uint64_t kNativeAudioTimelineSampleRate = 48000;
    constexpr std::uint64_t kNativeAudioMaxDurationFrames =
        kNativeAudioTimelineSampleRate * 60ull * 60ull * 4ull;
    constexpr std::uint32_t kNativeAudioMaxSubtitleCues = 50000;
    constexpr std::uint64_t kNativeAudioMaxSubtitleBytes = 8ull * 1024ull * 1024ull;

    enum class NativeAudioCodec : std::uint32_t
    {
        Wav = 1,
        Mp3 = 2,
        Ogg = 3,
        Flac = 4,
    };

    struct NativeAudioPeak
    {
        float minimum{};
        float maximum{};
    };

    struct NativeAudioCue
    {
        std::uint64_t start_frame{};
        std::uint64_t end_frame{};
        std::string text;

        friend bool operator==(const NativeAudioCue &, const NativeAudioCue &) = default;
    };

    struct NativeAudioData
    {
        NativeAudioCodec codec{NativeAudioCodec::Wav};
        std::uint32_t channels{2};
        std::uint32_t sample_rate{static_cast<std::uint32_t>(kNativeAudioTimelineSampleRate)};
        std::uint64_t duration_frames{};
        std::vector<std::byte> encoded_audio;
        std::vector<NativeAudioPeak> waveform;
        std::string subtitle_language;
        std::vector<NativeAudioCue> subtitles;
    };

    struct NativeAudioProduct
    {
        NativeAudioData data;
        ContentHash integrity_digest{};
        ContentHash product_hash{};
    };

    // File-backed view: encoded bytes stay in the immutable product file.
    // Metadata chunks are copied into bounded memory after streaming integrity
    // verification, so playback can seek without retaining the whole payload.
    struct NativeAudioFileProduct
    {
        std::filesystem::path path;
        NativeAudioData metadata;
        std::uint64_t encoded_audio_offset{};
        std::uint64_t encoded_audio_size{};
        ContentHash integrity_digest{};
        ContentHash product_hash{};

        std::vector<std::string_view> SubtitleTextAt(std::uint64_t frame) const;
    };

    enum class NativeAudioErrorCode : std::uint8_t
    {
        InvalidArgument,
        Truncated,
        UnsupportedVersion,
        UnsupportedFeatures,
        Overflow,
        InvalidDirectory,
        InvalidValue,
        InvalidUtf8,
        IntegrityMismatch,
    };

    class NativeAudioError final : public std::runtime_error
    {
    public:
        NativeAudioError(NativeAudioErrorCode code, std::string message);
        NativeAudioErrorCode Code() const noexcept;

    private:
        NativeAudioErrorCode code_{};
    };

    std::vector<std::byte> SerializeNativeAudio(const NativeAudioData &data);
    NativeAudioProduct DeserializeNativeAudio(
        std::span<const std::byte> bytes,
        const ContentHashPair *verified_hashes = nullptr);
    NativeAudioFileProduct ReadNativeAudioFile(const std::filesystem::path &path);
    void ValidateNativeAudioProductStructure(
        std::span<const std::byte> bytes,
        const ContentHashPair *verified_hashes = nullptr);
    ContentHash ComputeNativeAudioProductHash(std::span<const std::byte> bytes);
}

#endif
