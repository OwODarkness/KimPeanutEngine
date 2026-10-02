#include "native_audio.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace kpengine::asset
{
    namespace
    {
        constexpr std::array<std::uint8_t, 8> kMagic{{'K', 'P', 'A', 'U', 'D', 'I', 'O', '\0'}};
        constexpr std::uint32_t kNoRequiredFeatures = 0;
        constexpr std::uint32_t kChunkRequired = 1u;
        constexpr std::uint32_t kAudioChunk = 1;
        constexpr std::uint32_t kMetadataChunk = 2;
        constexpr std::uint32_t kWaveformChunk = 3;
        constexpr std::uint32_t kSubtitleChunk = 4;
        constexpr std::size_t kMetadataBytes = 24;
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

        struct Chunk
        {
            std::uint32_t type{};
            std::uint32_t flags{};
            std::uint64_t offset{};
            std::uint64_t size{};
        };

        [[noreturn]] void Fail(NativeAudioErrorCode code, const char *message)
        {
            throw NativeAudioError(code, message);
        }

        void AppendByte(std::vector<std::byte> &bytes, std::uint8_t value)
        {
            bytes.push_back(static_cast<std::byte>(value));
        }

        void AppendU16(std::vector<std::byte> &bytes, std::uint16_t value)
        {
            AppendByte(bytes, static_cast<std::uint8_t>(value));
            AppendByte(bytes, static_cast<std::uint8_t>(value >> 8));
        }

        void AppendU32(std::vector<std::byte> &bytes, std::uint32_t value)
        {
            for (std::size_t shift = 0; shift < 32; shift += 8)
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
        }

        void AppendU64(std::vector<std::byte> &bytes, std::uint64_t value)
        {
            for (std::size_t shift = 0; shift < 64; shift += 8)
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
        }

        std::uint16_t ReadU16(std::span<const std::byte> bytes, std::size_t offset)
        {
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8;
        }

        std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
        {
            std::uint32_t result = 0;
            for (std::size_t index = 0; index < 4; ++index)
                result |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                          << (index * 8);
            return result;
        }

        std::uint64_t ReadU64(std::span<const std::byte> bytes, std::size_t offset)
        {
            std::uint64_t result = 0;
            for (std::size_t index = 0; index < 8; ++index)
                result |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                          << (index * 8);
            return result;
        }

        void WriteAtHash(std::vector<std::byte> &bytes, std::size_t offset,
                         const ContentHash &hash)
        {
            if (offset > bytes.size() || bytes.size() - offset < hash.bytes.size())
                Fail(NativeAudioErrorCode::InvalidArgument, "native audio digest offset is invalid");
            for (std::size_t index = 0; index < hash.bytes.size(); ++index)
                bytes[offset + index] = static_cast<std::byte>(hash.bytes[index]);
        }

        ContentHash ReadHash(std::span<const std::byte> bytes, std::size_t offset)
        {
            ContentHash result{};
            for (std::size_t index = 0; index < result.bytes.size(); ++index)
                result.bytes[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
            return result;
        }

        bool IsValidUtf8(std::string_view value) noexcept
        {
            std::size_t index = 0;
            while (index < value.size())
            {
                const auto first = static_cast<std::uint8_t>(value[index]);
                if (first <= 0x7f) { ++index; continue; }
                std::size_t count = 0;
                std::uint32_t codepoint = 0;
                if (first >= 0xc2 && first <= 0xdf) { count = 2; codepoint = first & 0x1fu; }
                else if (first >= 0xe0 && first <= 0xef) { count = 3; codepoint = first & 0x0fu; }
                else if (first >= 0xf0 && first <= 0xf4) { count = 4; codepoint = first & 0x07u; }
                else return false;
                if (count > value.size() - index) return false;
                for (std::size_t tail = 1; tail < count; ++tail)
                {
                    const auto next = static_cast<std::uint8_t>(value[index + tail]);
                    if ((next & 0xc0u) != 0x80u) return false;
                    codepoint = (codepoint << 6) | (next & 0x3fu);
                }
                if ((count == 3 && codepoint < 0x800) ||
                    (count == 4 && codepoint < 0x10000) ||
                    codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                index += count;
            }
            return true;
        }

        bool IsKnownCodec(std::uint32_t codec) noexcept
        {
            return codec == static_cast<std::uint32_t>(NativeAudioCodec::Wav) ||
                   codec == static_cast<std::uint32_t>(NativeAudioCodec::Mp3) ||
                   codec == static_cast<std::uint32_t>(NativeAudioCodec::Flac);
        }

        void ValidateData(const NativeAudioData &data)
        {
            if (!IsKnownCodec(static_cast<std::uint32_t>(data.codec)) || data.channels != 2 ||
                data.sample_rate != kNativeAudioTimelineSampleRate || data.duration_frames == 0 ||
                data.duration_frames > kNativeAudioMaxDurationFrames || data.encoded_audio.empty() ||
                data.encoded_audio.size() > kNativeAudioMaxSourceBytes ||
                data.waveform.size() != kNativeAudioWaveformBucketCount ||
                data.subtitles.size() > kNativeAudioMaxSubtitleCues ||
                data.subtitle_language.size() > 63 ||
                !IsValidUtf8(data.subtitle_language) ||
                data.subtitle_language.find('\0') != std::string::npos)
            {
                Fail(NativeAudioErrorCode::InvalidValue, "native audio metadata is invalid");
            }
            if (data.subtitles.empty() != data.subtitle_language.empty())
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle language is inconsistent");
            for (const NativeAudioPeak &peak : data.waveform)
            {
                if (!std::isfinite(peak.minimum) || !std::isfinite(peak.maximum) ||
                    peak.minimum < -1.0f || peak.maximum > 1.0f || peak.minimum > peak.maximum)
                    Fail(NativeAudioErrorCode::InvalidValue, "native audio waveform peak is invalid");
            }
            std::uint64_t previous_start = 0;
            std::uint64_t subtitle_bytes = 0;
            for (std::size_t index = 0; index < data.subtitles.size(); ++index)
            {
                const NativeAudioCue &cue = data.subtitles[index];
                if (cue.start_frame >= cue.end_frame || cue.end_frame > data.duration_frames ||
                    cue.text.empty() || cue.text.size() > kNativeAudioMaxSubtitleBytes ||
                    !IsValidUtf8(cue.text) || cue.text.find('\0') != std::string::npos ||
                    (index > 0 && cue.start_frame < previous_start))
                    Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle cue is invalid");
                if (cue.text.size() > kNativeAudioMaxSubtitleBytes - subtitle_bytes)
                    Fail(NativeAudioErrorCode::Overflow, "native audio subtitle data exceeds its limit");
                subtitle_bytes += cue.text.size();
                previous_start = cue.start_frame;
            }
            if (subtitle_bytes > kNativeAudioMaxSubtitleBytes - data.subtitle_language.size())
                Fail(NativeAudioErrorCode::Overflow, "native audio subtitle data exceeds its limit");
        }

        std::map<std::uint32_t, Chunk> ReadDirectory(std::span<const std::byte> bytes)
        {
            if (bytes.size() < kNativeAudioHeaderSize || bytes.size() > kNativeAudioMaxProductBytes)
                Fail(NativeAudioErrorCode::Truncated, "native audio product size is invalid");
            for (std::size_t index = 0; index < kMagic.size(); ++index)
                if (std::to_integer<std::uint8_t>(bytes[index]) != kMagic[index])
                    Fail(NativeAudioErrorCode::InvalidArgument, "native audio magic is invalid");
            if (ReadU16(bytes, 8) != kNativeAudioVersion)
                Fail(NativeAudioErrorCode::UnsupportedVersion, "native audio version is unsupported");
            if (ReadU16(bytes, 10) != kNativeAudioHeaderSize || ReadU32(bytes, 12) != kNoRequiredFeatures)
                Fail(NativeAudioErrorCode::UnsupportedFeatures, "native audio header features are unsupported");
            const std::uint64_t total_size = ReadU64(bytes, 16);
            const std::uint64_t directory_offset = ReadU64(bytes, 24);
            const std::uint32_t count = ReadU32(bytes, 32);
            if (total_size != bytes.size() || directory_offset != kNativeAudioHeaderSize ||
                ReadU32(bytes, 36) != 0 || count < 3 || count > 64)
                Fail(NativeAudioErrorCode::InvalidDirectory, "native audio directory header is invalid");
            const std::uint64_t directory_size = static_cast<std::uint64_t>(count) *
                                                  kNativeAudioDirectoryEntrySize;
            if (directory_size > bytes.size() - kNativeAudioHeaderSize)
                Fail(NativeAudioErrorCode::Truncated, "native audio directory is truncated");
            const std::uint64_t payload_start = kNativeAudioHeaderSize + directory_size;
            std::map<std::uint32_t, Chunk> chunks;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::size_t offset = kNativeAudioHeaderSize +
                                           static_cast<std::size_t>(index) * kNativeAudioDirectoryEntrySize;
                Chunk chunk{ReadU32(bytes, offset), ReadU32(bytes, offset + 4),
                            ReadU64(bytes, offset + 8), ReadU64(bytes, offset + 16)};
                const std::uint64_t reserved = ReadU64(bytes, offset + 24);
                if (chunk.type == 0 || (chunk.flags & ~kChunkRequired) != 0 || reserved != 0 ||
                    chunk.offset < payload_start || chunk.offset > total_size ||
                    chunk.size > total_size - chunk.offset || chunk.size == 0)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "native audio chunk range is invalid");
                if (!chunks.emplace(chunk.type, chunk).second)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "native audio directory has duplicate chunks");
                if ((chunk.type == kAudioChunk || chunk.type == kMetadataChunk || chunk.type == kWaveformChunk) &&
                    (chunk.flags & kChunkRequired) == 0)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "required native audio chunk is not marked required");
                if (chunk.type == kSubtitleChunk && (chunk.flags & kChunkRequired) != 0)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "optional subtitle chunk is marked required");
                if (chunk.type > kSubtitleChunk && (chunk.flags & kChunkRequired) != 0)
                    Fail(NativeAudioErrorCode::UnsupportedFeatures, "native audio has an unknown required chunk");
                ranges.emplace_back(chunk.offset, chunk.offset + chunk.size);
            }
            for (const std::uint32_t required : {kAudioChunk, kMetadataChunk, kWaveformChunk})
                if (!chunks.contains(required))
                    Fail(NativeAudioErrorCode::InvalidDirectory, "native audio product is missing a required chunk");
            std::sort(ranges.begin(), ranges.end());
            std::uint64_t expected_offset = payload_start;
            for (std::size_t index = 1; index < ranges.size(); ++index)
            {
                if (ranges[index].first != ranges[index - 1].second)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "native audio chunk spans are not canonical");
            }
            for (const auto &range : ranges)
            {
                if (range.first != expected_offset)
                    Fail(NativeAudioErrorCode::InvalidDirectory, "native audio chunk spans are not canonical");
                expected_offset = range.second;
            }
            if (expected_offset != total_size)
                Fail(NativeAudioErrorCode::InvalidDirectory, "native audio product has unreferenced trailing bytes");
            return chunks;
        }

        void ValidateDigest(std::span<const std::byte> bytes,
                            const ContentHashPair *verified_hashes)
        {
            ContentHashPair hashes{};
            if (verified_hashes != nullptr) hashes = *verified_hashes;
            else
            {
                const auto computed = Sha256WithZeroedRange(bytes, kNativeAudioDigestOffset,
                                                             kNativeAudioDigestSize);
                if (!computed) Fail(NativeAudioErrorCode::Truncated, "native audio digest is truncated");
                hashes = *computed;
            }
            if (ReadHash(bytes, kNativeAudioDigestOffset) != hashes.zeroed_range_hash)
                Fail(NativeAudioErrorCode::IntegrityMismatch, "native audio integrity digest mismatch");
        }

        std::span<const std::byte> ChunkBytes(std::span<const std::byte> bytes, const Chunk &chunk)
        {
            return bytes.subspan(static_cast<std::size_t>(chunk.offset),
                                 static_cast<std::size_t>(chunk.size));
        }

        void ValidatePayloads(std::span<const std::byte> bytes,
                              const std::map<std::uint32_t, Chunk> &chunks)
        {
            const auto metadata = ChunkBytes(bytes, chunks.at(kMetadataChunk));
            if (metadata.size() != kMetadataBytes || ReadU32(metadata, 12) != 0 ||
                !IsKnownCodec(ReadU32(metadata, 0)) || ReadU32(metadata, 4) != 2 ||
                ReadU32(metadata, 8) != kNativeAudioTimelineSampleRate ||
                ReadU64(metadata, 16) == 0 ||
                ReadU64(metadata, 16) > kNativeAudioMaxDurationFrames)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio metadata chunk is invalid");
            if (chunks.at(kAudioChunk).size == 0 ||
                chunks.at(kAudioChunk).size > kNativeAudioMaxSourceBytes)
                Fail(NativeAudioErrorCode::Overflow, "native audio payload exceeds its limit");

            const auto waveform = ChunkBytes(bytes, chunks.at(kWaveformChunk));
            if (waveform.size() != kNativeAudioWaveformBucketCount * 8u)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio waveform chunk size is invalid");
            for (std::size_t offset = 0; offset < waveform.size(); offset += 8)
            {
                const float minimum = std::bit_cast<float>(ReadU32(waveform, offset));
                const float maximum = std::bit_cast<float>(ReadU32(waveform, offset + 4));
                if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < -1.0f ||
                    maximum > 1.0f || minimum > maximum)
                    Fail(NativeAudioErrorCode::InvalidValue, "native audio waveform peak is invalid");
            }

            const auto subtitle = chunks.find(kSubtitleChunk);
            if (subtitle == chunks.end()) return;
            const auto payload = ChunkBytes(bytes, subtitle->second);
            if (payload.size() > kNativeAudioMaxSubtitleBytes || payload.size() < 8)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle chunk size is invalid");
            const std::uint16_t language_size = ReadU16(payload, 0);
            const std::uint32_t cue_count = ReadU32(payload, 4);
            if (ReadU16(payload, 2) != 0 || language_size == 0 || language_size > 63 ||
                cue_count == 0 || cue_count > kNativeAudioMaxSubtitleCues ||
                language_size > payload.size() - 8)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle header is invalid");
            const std::uint64_t duration = ReadU64(metadata, 16);
            std::size_t cursor = 8;
            std::uint64_t previous_start = 0;
            for (std::uint32_t index = 0; index < cue_count; ++index)
            {
                if (payload.size() - cursor < 24)
                    Fail(NativeAudioErrorCode::Truncated, "native audio subtitle cue is truncated");
                const std::uint64_t start = ReadU64(payload, cursor);
                const std::uint64_t end = ReadU64(payload, cursor + 8);
                const std::uint32_t text_size = ReadU32(payload, cursor + 16);
                if (ReadU32(payload, cursor + 20) != 0 || start >= end || end > duration ||
                    text_size == 0 || text_size > payload.size() - cursor - 24)
                    Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle cue range is invalid");
                cursor += 24;
                const std::string_view text_view{
                    reinterpret_cast<const char *>(payload.data() + cursor), text_size};
                if (!IsValidUtf8(text_view) || text_view.find('\0') != std::string_view::npos ||
                    (index > 0 && start < previous_start))
                    Fail(NativeAudioErrorCode::InvalidUtf8, "native audio subtitle cue text or order is invalid");
                previous_start = start;
                cursor += text_size;
            }
            if (language_size != payload.size() - cursor)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle language range is invalid");
            const std::string_view language{
                reinterpret_cast<const char *>(payload.data() + cursor), language_size};
            if (!IsValidUtf8(language) || language.find('\0') != std::string_view::npos)
                Fail(NativeAudioErrorCode::InvalidUtf8, "native audio subtitle language is invalid");
        }
    }

    NativeAudioError::NativeAudioError(NativeAudioErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    NativeAudioErrorCode NativeAudioError::Code() const noexcept { return code_; }

    std::vector<std::byte> SerializeNativeAudio(const NativeAudioData &data)
    {
        ValidateData(data);
        std::vector<std::byte> metadata;
        metadata.reserve(kMetadataBytes);
        AppendU32(metadata, static_cast<std::uint32_t>(data.codec));
        AppendU32(metadata, data.channels);
        AppendU32(metadata, data.sample_rate);
        AppendU32(metadata, 0);
        AppendU64(metadata, data.duration_frames);

        std::vector<std::byte> waveform;
        waveform.reserve(data.waveform.size() * 8);
        for (const NativeAudioPeak &peak : data.waveform)
        {
            AppendU32(waveform, std::bit_cast<std::uint32_t>(peak.minimum));
            AppendU32(waveform, std::bit_cast<std::uint32_t>(peak.maximum));
        }

        std::vector<std::byte> subtitles;
        if (!data.subtitles.empty())
        {
            AppendU16(subtitles, static_cast<std::uint16_t>(data.subtitle_language.size()));
            AppendU16(subtitles, 0);
            AppendU32(subtitles, static_cast<std::uint32_t>(data.subtitles.size()));
            for (const NativeAudioCue &cue : data.subtitles)
            {
                AppendU64(subtitles, cue.start_frame);
                AppendU64(subtitles, cue.end_frame);
                AppendU32(subtitles, static_cast<std::uint32_t>(cue.text.size()));
                AppendU32(subtitles, 0);
                for (const unsigned char character : cue.text) AppendByte(subtitles, character);
            }
            for (const unsigned char character : data.subtitle_language) AppendByte(subtitles, character);
            if (subtitles.size() > kNativeAudioMaxSubtitleBytes)
                Fail(NativeAudioErrorCode::Overflow, "native audio subtitle chunk exceeds its limit");
        }

        struct PayloadView
        {
            std::uint32_t type{};
            std::uint32_t flags{};
            std::span<const std::byte> bytes;
        };
        std::vector<PayloadView> payloads{{kAudioChunk, kChunkRequired, data.encoded_audio},
                                          {kMetadataChunk, kChunkRequired, metadata},
                                          {kWaveformChunk, kChunkRequired, waveform}};
        if (!subtitles.empty()) payloads.push_back({kSubtitleChunk, 0, subtitles});
        std::vector<Chunk> chunks;
        chunks.reserve(payloads.size());
        for (const PayloadView &payload : payloads)
            chunks.push_back({payload.type, payload.flags, 0, payload.bytes.size()});
        const std::uint64_t directory_end = kNativeAudioHeaderSize +
            static_cast<std::uint64_t>(chunks.size()) * kNativeAudioDirectoryEntrySize;
        std::uint64_t cursor = directory_end;
        for (Chunk &chunk : chunks)
        {
            chunk.offset = cursor;
            if (chunk.size > kNativeAudioMaxProductBytes - cursor)
                Fail(NativeAudioErrorCode::Overflow, "native audio product exceeds its size limit");
            cursor += chunk.size;
        }
        if (cursor > kNativeAudioMaxProductBytes || cursor > std::numeric_limits<std::size_t>::max())
            Fail(NativeAudioErrorCode::Overflow, "native audio product exceeds its size limit");

        std::vector<std::byte> bytes;
        bytes.reserve(static_cast<std::size_t>(cursor));
        for (const std::uint8_t value : kMagic) AppendByte(bytes, value);
        AppendU16(bytes, kNativeAudioVersion);
        AppendU16(bytes, static_cast<std::uint16_t>(kNativeAudioHeaderSize));
        AppendU32(bytes, kNoRequiredFeatures);
        AppendU64(bytes, cursor);
        AppendU64(bytes, kNativeAudioHeaderSize);
        AppendU32(bytes, static_cast<std::uint32_t>(chunks.size()));
        AppendU32(bytes, 0);
        for (std::size_t index = 0; index < kNativeAudioDigestSize; ++index) AppendByte(bytes, 0);
        for (const Chunk &chunk : chunks)
        {
            AppendU32(bytes, chunk.type);
            AppendU32(bytes, chunk.flags);
            AppendU64(bytes, chunk.offset);
            AppendU64(bytes, chunk.size);
            AppendU64(bytes, 0);
        }
        for (const PayloadView &payload : payloads)
            bytes.insert(bytes.end(), payload.bytes.begin(), payload.bytes.end());
        const auto digest = Sha256WithZeroedRange(bytes, kNativeAudioDigestOffset,
                                                   kNativeAudioDigestSize);
        if (!digest) Fail(NativeAudioErrorCode::InvalidArgument, "failed to hash native audio product");
        WriteAtHash(bytes, kNativeAudioDigestOffset, digest->zeroed_range_hash);
        return bytes;
    }

    NativeAudioProduct DeserializeNativeAudio(std::span<const std::byte> bytes,
                                              const ContentHashPair *verified_hashes)
    {
        const auto chunks = ReadDirectory(bytes);
        ValidateDigest(bytes, verified_hashes);
        ValidatePayloads(bytes, chunks);
        const auto metadata = ChunkBytes(bytes, chunks.at(kMetadataChunk));
        if (metadata.size() != kMetadataBytes || ReadU32(metadata, 12) != 0)
            Fail(NativeAudioErrorCode::InvalidValue, "native audio metadata chunk is invalid");
        NativeAudioData data{};
        data.codec = static_cast<NativeAudioCodec>(ReadU32(metadata, 0));
        data.channels = ReadU32(metadata, 4);
        data.sample_rate = ReadU32(metadata, 8);
        data.duration_frames = ReadU64(metadata, 16);
        const auto audio = ChunkBytes(bytes, chunks.at(kAudioChunk));
        if (audio.size() > kNativeAudioMaxSourceBytes)
            Fail(NativeAudioErrorCode::Overflow, "native audio payload exceeds its limit");
        data.encoded_audio.assign(audio.begin(), audio.end());

        const auto waveform = ChunkBytes(bytes, chunks.at(kWaveformChunk));
        if (waveform.size() != kNativeAudioWaveformBucketCount * 8u)
            Fail(NativeAudioErrorCode::InvalidValue, "native audio waveform chunk size is invalid");
        data.waveform.reserve(kNativeAudioWaveformBucketCount);
        for (std::size_t offset = 0; offset < waveform.size(); offset += 8)
            data.waveform.push_back({std::bit_cast<float>(ReadU32(waveform, offset)),
                                     std::bit_cast<float>(ReadU32(waveform, offset + 4))});

        if (const auto subtitle = chunks.find(kSubtitleChunk); subtitle != chunks.end())
        {
            const auto payload = ChunkBytes(bytes, subtitle->second);
            if (payload.size() > kNativeAudioMaxSubtitleBytes || payload.size() < 8)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle chunk size is invalid");
            const std::uint16_t language_size = ReadU16(payload, 0);
            const std::uint32_t cue_count = ReadU32(payload, 4);
            if (ReadU16(payload, 2) != 0 || language_size > 63 ||
                cue_count == 0 || cue_count > kNativeAudioMaxSubtitleCues ||
                language_size > payload.size() - 8)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle header is invalid");
            std::size_t cursor = 8;
            data.subtitles.reserve(cue_count);
            for (std::uint32_t index = 0; index < cue_count; ++index)
            {
                if (payload.size() - cursor < 24)
                    Fail(NativeAudioErrorCode::Truncated, "native audio subtitle cue is truncated");
                NativeAudioCue cue{};
                cue.start_frame = ReadU64(payload, cursor);
                cue.end_frame = ReadU64(payload, cursor + 8);
                const std::uint32_t text_size = ReadU32(payload, cursor + 16);
                if (ReadU32(payload, cursor + 20) != 0 || text_size > payload.size() - cursor - 24)
                    Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle text range is invalid");
                cursor += 24;
                cue.text.assign(reinterpret_cast<const char *>(payload.data() + cursor), text_size);
                cursor += text_size;
                data.subtitles.push_back(std::move(cue));
            }
            if (language_size != payload.size() - cursor)
                Fail(NativeAudioErrorCode::InvalidValue, "native audio subtitle language range is invalid");
            data.subtitle_language.assign(reinterpret_cast<const char *>(payload.data() + cursor), language_size);
        }
        ValidateData(data);
        return {std::move(data), ReadHash(bytes, kNativeAudioDigestOffset), Sha256(bytes)};
    }

    void ValidateNativeAudioProductStructure(std::span<const std::byte> bytes,
                                             const ContentHashPair *verified_hashes)
    {
        const auto chunks = ReadDirectory(bytes);
        ValidateDigest(bytes, verified_hashes);
        ValidatePayloads(bytes, chunks);
    }

    ContentHash ComputeNativeAudioProductHash(std::span<const std::byte> bytes)
    {
        return Sha256(bytes);
    }
}
