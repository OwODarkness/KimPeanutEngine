#include "audio_importer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <miniaudio/miniaudio.h>

namespace kpengine::asset
{
    namespace
    {
        constexpr std::uint64_t kFramesPerMillisecond = 48;
        constexpr ma_uint64 kDecodeBlockFrames = 2048;

        bool IsValidUtf8(std::string_view value) noexcept;

        [[noreturn]] void Fail(std::string message)
        {
            throw AudioImportError(std::move(message));
        }

        std::filesystem::path ResolveInput(const std::filesystem::path &asset_root,
                                           const std::filesystem::path &relative_path,
                                           std::string_view label)
        {
            if (asset_root.empty() || relative_path.empty() || relative_path.is_absolute())
                Fail(std::string{label} + " path must be relative to the Asset root");
            std::string normalized;
            try
            {
                const std::u8string utf8_path = relative_path.generic_u8string();
                normalized = NormalizeAssetRelativePath(std::string{
                    reinterpret_cast<const char *>(utf8_path.data()), utf8_path.size()});
            }
            catch (const std::exception &error)
            {
                Fail(std::string{label} + " path is invalid: " + error.what());
            }
            std::error_code error;
            const auto root = std::filesystem::weakly_canonical(asset_root, error);
            if (error || !std::filesystem::is_directory(root))
                Fail("Asset root does not exist or cannot be resolved");
            const std::u8string normalized_u8{
                reinterpret_cast<const char8_t *>(normalized.data()), normalized.size()};
            const auto path = std::filesystem::weakly_canonical(root / std::filesystem::path{normalized_u8}, error);
            if (error || !std::filesystem::is_regular_file(path))
                Fail(std::string{label} + " file does not exist or cannot be resolved");
            const auto relative = path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                Fail(std::string{label} + " path resolves outside the Asset root");
            return path;
        }

        std::vector<std::byte> ReadBounded(const std::filesystem::path &path,
                                           std::uint64_t max_bytes,
                                           std::string_view label)
        {
            std::error_code error;
            const std::uint64_t size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > max_bytes || size > std::numeric_limits<std::size_t>::max())
                Fail(std::string{label} + " file is empty, unreadable, or exceeds its size limit");
            std::ifstream input(path, std::ios::binary);
            if (!input)
                Fail("cannot open " + std::string{label} + " file");
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!input || input.peek() != std::char_traits<char>::eof())
                Fail(std::string{label} + " file changed while it was being read");
            return bytes;
        }

        std::string ReadSubtitle(const std::filesystem::path &path)
        {
            const auto bytes = ReadBounded(path, kNativeAudioMaxSubtitleBytes, "subtitle");
            std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
            if (!IsValidUtf8(text))
                Fail("subtitle is not valid UTF-8");
            std::string normalized;
            normalized.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\r')
                {
                    normalized.push_back('\n');
                    if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
                }
                else
                {
                    normalized.push_back(text[index]);
                }
            }
            return normalized;
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
                    (count == 4 && codepoint < 0x10000) || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
                index += count;
            }
            return true;
        }

        std::string_view Trim(std::string_view value)
        {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            return value;
        }

        std::uint64_t ParseTimestamp(std::string_view value, char decimal_separator)
        {
            value = Trim(value);
            const std::size_t decimal = value.find(decimal_separator);
            if (decimal == std::string_view::npos || value.size() - decimal - 1 != 3)
                Fail("subtitle timestamp must include millisecond precision");
            const std::string_view clock = value.substr(0, decimal);
            const std::string_view milliseconds = value.substr(decimal + 1);
            const std::size_t first_colon = clock.find(':');
            const std::size_t last_colon = clock.rfind(':');
            if (first_colon == std::string_view::npos)
                Fail("subtitle timestamp is malformed");
            const bool has_hours = first_colon != last_colon;
            const std::size_t hours_end = has_hours ? first_colon : 0;
            const std::size_t minutes_begin = has_hours ? first_colon + 1 : 0;
            const std::size_t minutes_end = last_colon;
            const std::size_t seconds_begin = last_colon + 1;
            auto parse_part = [](std::string_view part) -> std::uint64_t
            {
                std::uint64_t result = 0;
                const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result);
                if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size())
                    Fail("subtitle timestamp has a non-numeric field");
                return result;
            };
            const std::uint64_t hours = has_hours ? parse_part(clock.substr(0, hours_end)) : 0;
            const std::uint64_t minutes = parse_part(clock.substr(minutes_begin, minutes_end - minutes_begin));
            const std::uint64_t seconds = parse_part(clock.substr(seconds_begin));
            const std::uint64_t millis = parse_part(milliseconds);
            if (minutes > 59 || seconds > 59 || millis > 999 || hours > 9999)
                Fail("subtitle timestamp field is out of range");
            constexpr std::uint64_t kMillisPerHour = 3'600'000;
            const std::uint64_t total_ms = hours * kMillisPerHour + minutes * 60'000 + seconds * 1'000 + millis;
            if (total_ms > kNativeAudioMaxDurationFrames / kFramesPerMillisecond)
                Fail("subtitle timestamp exceeds the four-hour duration limit");
            return total_ms;
        }

        std::uint64_t ParseLrcTimestamp(std::string_view value)
        {
            value = Trim(value);
            const std::size_t colon = value.find(':');
            const std::size_t decimal = value.find('.');
            if (colon == std::string_view::npos || decimal == std::string_view::npos ||
                colon == 0 || decimal <= colon + 1 || decimal + 1 == value.size())
                Fail("LRC timestamp is malformed");
            auto parse_part = [](std::string_view part) -> std::uint64_t
            {
                std::uint64_t result = 0;
                const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result);
                if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size())
                    Fail("LRC timestamp has a non-numeric field");
                return result;
            };
            const std::uint64_t minutes = parse_part(value.substr(0, colon));
            const std::uint64_t seconds = parse_part(value.substr(colon + 1, decimal - colon - 1));
            const std::string_view fraction = value.substr(decimal + 1);
            if (seconds > 59 || fraction.size() > 3)
                Fail("LRC timestamp field is out of range");
            std::uint64_t millis = parse_part(fraction);
            if (fraction.size() == 1) millis *= 100;
            else if (fraction.size() == 2) millis *= 10;
            if (minutes > kNativeAudioMaxDurationFrames /
                              (kFramesPerMillisecond * 60ull))
                Fail("LRC timestamp exceeds the four-hour duration limit");
            return minutes * 60'000 + seconds * 1'000 + millis;
        }

        std::vector<NativeAudioCue> ParseLrc(std::string_view source,
                                             std::uint64_t duration_frames)
        {
            std::int64_t offset_ms = 0;
            std::vector<std::pair<std::uint64_t, std::string>> timed_lines;
            for (std::size_t begin = 0; begin <= source.size();)
            {
                const std::size_t end = source.find('\n', begin);
                const std::string_view line = Trim(source.substr(begin,
                    end == std::string_view::npos ? source.size() - begin : end - begin));
                if (!line.empty() && line.front() == '[')
                {
                    std::size_t cursor = 0;
                    std::vector<std::uint64_t> timestamps;
                    bool is_metadata_only = false;
                    while (cursor < line.size() && line[cursor] == '[')
                    {
                        const std::size_t close = line.find(']', cursor + 1);
                        if (close == std::string_view::npos) break;
                        const std::string_view tag = line.substr(cursor + 1, close - cursor - 1);
                        const std::size_t colon = tag.find(':');
                        if (tag.find('.') != std::string_view::npos)
                        {
                            timestamps.push_back(ParseLrcTimestamp(tag));
                        }
                        else if (colon != std::string_view::npos)
                        {
                            std::string key(tag.substr(0, colon));
                            std::transform(key.begin(), key.end(), key.begin(),
                                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                            if (key == "offset")
                            {
                                std::string_view value = Trim(tag.substr(colon + 1));
                                bool positive_sign = false;
                                if (!value.empty() && value.front() == '+')
                                {
                                    positive_sign = true;
                                    value.remove_prefix(1);
                                }
                                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), offset_ms);
                                if (value.empty() || parsed.ec != std::errc{} ||
                                    parsed.ptr != value.data() + value.size() ||
                                    (positive_sign && offset_ms < 0) ||
                                    offset_ms < -4ll * 60 * 60 * 1000 || offset_ms > 4ll * 60 * 60 * 1000)
                                    Fail("LRC offset is invalid");
                                is_metadata_only = true;
                            }
                            else if (key == "ar" || key == "ti" || key == "al" || key == "by" ||
                                     key == "re" || key == "ve" || key == "length" || key == "la")
                            {
                                is_metadata_only = true;
                            }
                            else
                            {
                                Fail("LRC contains an unsupported metadata tag");
                            }
                        }
                        else if (timestamps.empty())
                            Fail("LRC timestamp is malformed");
                        else
                            break;
                        cursor = close + 1;
                    }
                    const std::string_view lyric = Trim(line.substr(cursor));
                    if (!timestamps.empty() && !lyric.empty())
                    {
                        for (const std::uint64_t timestamp : timestamps)
                        {
                            const std::int64_t adjusted = static_cast<std::int64_t>(timestamp) + offset_ms;
                            if (adjusted < 0 || static_cast<std::uint64_t>(adjusted) >
                                                    kNativeAudioMaxDurationFrames / kFramesPerMillisecond)
                                Fail("LRC cue time is outside the supported timeline");
                            timed_lines.emplace_back(static_cast<std::uint64_t>(adjusted), std::string{lyric});
                        }
                    }
                    else if (!timestamps.empty() && lyric.empty() && !is_metadata_only)
                    {
                        Fail("LRC cue has no lyric text");
                    }
                }
                else if (!line.empty())
                {
                    Fail("LRC contains a line without a timestamp or metadata tag");
                }
                if (end == std::string_view::npos) break;
                begin = end + 1;
            }
            if (timed_lines.empty()) Fail("LRC contains no timed lyric lines");
            std::stable_sort(timed_lines.begin(), timed_lines.end(), [](const auto &left, const auto &right)
            {
                return left.first < right.first;
            });
            if (timed_lines.size() > kNativeAudioMaxSubtitleCues)
                Fail("LRC has more than 50,000 timed lines");
            std::vector<NativeAudioCue> cues;
            cues.reserve(timed_lines.size());
            for (std::size_t index = 0; index < timed_lines.size(); ++index)
            {
                const std::uint64_t start_frame = timed_lines[index].first * kFramesPerMillisecond;
                if (start_frame >= duration_frames)
                    Fail("LRC cue starts at or beyond the decoded audio duration");
                std::size_t next = index + 1;
                while (next < timed_lines.size() && timed_lines[next].first == timed_lines[index].first) ++next;
                const std::uint64_t end_frame = next < timed_lines.size()
                    ? timed_lines[next].first * kFramesPerMillisecond : duration_frames;
                cues.push_back({start_frame, end_frame, timed_lines[index].second});
            }
            return cues;
        }

        bool IsTimestampLine(std::string_view line, char decimal_separator)
        {
            const std::size_t arrow = line.find("-->");
            if (arrow == std::string_view::npos) return false;
            try
            {
                static_cast<void>(ParseTimestamp(line.substr(0, arrow), decimal_separator));
                return true;
            }
            catch (const AudioImportError &)
            {
                return false;
            }
        }

        std::vector<NativeAudioCue> ParseSubtitles(std::string_view source,
                                                   std::string_view extension,
                                                   std::uint64_t duration_frames)
        {
            if (extension == ".lrc") return ParseLrc(source, duration_frames);
            const bool webvtt = extension == ".vtt" || extension == ".webvtt";
            const char decimal_separator = webvtt ? '.' : ',';
            std::vector<std::string_view> lines;
            for (std::size_t begin = 0; begin <= source.size();)
            {
                const std::size_t end = source.find('\n', begin);
                lines.push_back(Trim(source.substr(begin, end == std::string_view::npos
                                                           ? source.size() - begin : end - begin)));
                if (end == std::string_view::npos) break;
                begin = end + 1;
            }
            std::vector<NativeAudioCue> cues;
            std::size_t index = 0;
            while (index < lines.size())
            {
                while (index < lines.size() && lines[index].empty()) ++index;
                if (index >= lines.size()) break;
                if (webvtt && (lines[index].starts_with("WEBVTT") || lines[index].starts_with("NOTE") ||
                               lines[index] == "STYLE" || lines[index] == "REGION"))
                {
                    while (index < lines.size() && !lines[index].empty()) ++index;
                    continue;
                }
                std::size_t timing_line = index;
                if (!IsTimestampLine(lines[timing_line], decimal_separator) &&
                    timing_line + 1 < lines.size() &&
                    IsTimestampLine(lines[timing_line + 1], decimal_separator))
                    ++timing_line;
                if (timing_line >= lines.size() || !IsTimestampLine(lines[timing_line], decimal_separator))
                    Fail("subtitle contains a malformed cue block");
                const std::string_view timing = lines[timing_line];
                const std::size_t arrow = timing.find("-->");
                const std::uint64_t start_ms = ParseTimestamp(timing.substr(0, arrow), decimal_separator);
                std::string_view end_token = Trim(timing.substr(arrow + 3));
                const std::size_t setting = end_token.find_first_of(" \t");
                if (setting != std::string_view::npos) end_token = end_token.substr(0, setting);
                const std::uint64_t end_ms = ParseTimestamp(end_token, decimal_separator);
                if (end_ms <= start_ms)
                    Fail("subtitle cue end must follow its start");
                NativeAudioCue cue{};
                cue.start_frame = start_ms * kFramesPerMillisecond;
                cue.end_frame = end_ms * kFramesPerMillisecond;
                for (std::size_t text_line = timing_line + 1;
                     text_line < lines.size() && !lines[text_line].empty(); ++text_line)
                {
                    if (!cue.text.empty()) cue.text.push_back('\n');
                    cue.text.append(lines[text_line]);
                }
                if (cue.text.empty()) Fail("subtitle cue has no text");
                if (cue.end_frame > duration_frames)
                    Fail("subtitle cue extends past the decoded audio duration");
                cues.push_back(std::move(cue));
                if (cues.size() > kNativeAudioMaxSubtitleCues)
                    Fail("subtitle has more than 50,000 cues");
                index = timing_line + 1;
                while (index < lines.size() && !lines[index].empty()) ++index;
            }
            if (cues.empty()) Fail("subtitle contains no timed cues");
            std::stable_sort(cues.begin(), cues.end(), [](const auto &left, const auto &right)
            {
                return left.start_frame < right.start_frame;
            });
            return cues;
        }

        NativeAudioCodec CodecForExtension(std::string extension)
        {
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (extension == ".wav") return NativeAudioCodec::Wav;
            if (extension == ".mp3") return NativeAudioCodec::Mp3;
            if (extension == ".flac") return NativeAudioCodec::Flac;
            Fail("audio source extension must be WAV, MP3, or FLAC; OGG is not enabled by the current decoder build");
        }

        bool MatchesCodecHeader(std::span<const std::byte> bytes, NativeAudioCodec codec)
        {
            auto byte = [&](std::size_t index)
            {
                return index < bytes.size() ? std::to_integer<std::uint8_t>(bytes[index]) : 0;
            };
            switch (codec)
            {
            case NativeAudioCodec::Wav:
                return bytes.size() >= 12 && byte(0) == 'R' && byte(1) == 'I' &&
                       byte(2) == 'F' && byte(3) == 'F' && byte(8) == 'W' &&
                       byte(9) == 'A' && byte(10) == 'V' && byte(11) == 'E';
            case NativeAudioCodec::Mp3:
                return bytes.size() >= 3 &&
                    ((byte(0) == 'I' && byte(1) == 'D' && byte(2) == '3') ||
                     (byte(0) == 0xff && (byte(1) & 0xe0u) == 0xe0u));
            case NativeAudioCodec::Ogg:
                return bytes.size() >= 4 && byte(0) == 'O' && byte(1) == 'g' &&
                       byte(2) == 'g' && byte(3) == 'S';
            case NativeAudioCodec::Flac:
                return bytes.size() >= 4 && byte(0) == 'f' && byte(1) == 'L' &&
                       byte(2) == 'a' && byte(3) == 'C';
            }
            return false;
        }

        ma_result InitDecoder(const std::filesystem::path &path,
                              const ma_decoder_config &config, ma_decoder &decoder)
        {
#if defined(_WIN32)
            return ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
            const std::string utf8 = path.string();
            return ma_decoder_init_file(utf8.c_str(), &config, &decoder);
#endif
        }

        std::pair<std::uint64_t, std::vector<NativeAudioPeak>> AnalyzeAudio(
            const std::filesystem::path &path)
        {
            ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2,
                static_cast<ma_uint32>(kNativeAudioTimelineSampleRate));
            ma_decoder decoder{};
            if (InitDecoder(path, config, decoder) != MA_SUCCESS)
                Fail("audio decoder rejected the source file");
            struct DecoderGuard
            {
                ma_decoder *decoder{};
                ~DecoderGuard() { if (decoder != nullptr) ma_decoder_uninit(decoder); }
            } guard{&decoder};

            ma_uint64 total_frames = 0;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames) != MA_SUCCESS ||
                total_frames == 0 || total_frames > kNativeAudioMaxDurationFrames)
                Fail("audio duration is unknown, empty, or exceeds four hours");
            std::vector<NativeAudioPeak> peaks(kNativeAudioWaveformBucketCount,
                                               {1.0f, -1.0f});
            std::array<float, kDecodeBlockFrames * 2> block{};
            std::uint64_t decoded_frames = 0;
            while (decoded_frames < total_frames)
            {
                const ma_uint64 request = std::min<ma_uint64>(
                    kDecodeBlockFrames, total_frames - decoded_frames);
                ma_uint64 read = 0;
                const ma_result result = ma_decoder_read_pcm_frames(&decoder, block.data(), request, &read);
                if (result != MA_SUCCESS || read == 0)
                    Fail("audio decoder failed before the declared duration");
                for (ma_uint64 frame = 0; frame < read; ++frame)
                {
                    const float sample = std::clamp((block[frame * 2] + block[frame * 2 + 1]) * 0.5f,
                                                    -1.0f, 1.0f);
                    const std::uint64_t source_frame = decoded_frames + frame;
                    const std::size_t bucket = std::min<std::size_t>(
                        static_cast<std::size_t>(source_frame * kNativeAudioWaveformBucketCount / total_frames),
                        kNativeAudioWaveformBucketCount - 1);
                    peaks[bucket].minimum = std::min(peaks[bucket].minimum, sample);
                    peaks[bucket].maximum = std::max(peaks[bucket].maximum, sample);
                }
                decoded_frames += read;
            }
            if (decoded_frames != total_frames)
                Fail("decoded audio frame count does not match its declared duration");
            for (NativeAudioPeak &peak : peaks)
                if (peak.minimum > peak.maximum) peak = {};
            return {decoded_frames, std::move(peaks)};
        }
    }

    AudioImportError::AudioImportError(std::string message)
        : std::runtime_error(std::move(message))
    {
    }

    CookedAudio AudioImporter::Import(const AudioImportRequest &request) const
    {
        const auto source_path = ResolveInput(request.asset_root, request.source_path, "audio source");
        const NativeAudioCodec codec = CodecForExtension(source_path.extension().string());
        std::error_code error;
        const std::uint64_t source_size = std::filesystem::file_size(source_path, error);
        if (error || source_size == 0 || source_size > kNativeAudioMaxSourceBytes)
            Fail("audio source is empty, unreadable, or exceeds 512 MiB");
        const ContentHash source_hash = Sha256File(source_path);
        const auto [duration_frames, waveform] = AnalyzeAudio(source_path);

        NativeAudioData data{};
        data.codec = codec;
        data.channels = 2;
        data.sample_rate = static_cast<std::uint32_t>(kNativeAudioTimelineSampleRate);
        data.duration_frames = duration_frames;
        data.encoded_audio = ReadBounded(source_path, kNativeAudioMaxSourceBytes, "audio source");
        if (Sha256(data.encoded_audio) != source_hash)
            Fail("audio source changed while it was being decoded");
        if (!MatchesCodecHeader(data.encoded_audio, codec))
            Fail("audio source extension does not match its encoded format");
        data.waveform = waveform;
        if (request.options.subtitle_path.has_value())
        {
            const auto subtitle_path = ResolveInput(request.asset_root,
                *request.options.subtitle_path, "subtitle");
            std::string extension = subtitle_path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (extension != ".srt" && extension != ".vtt" && extension != ".webvtt" &&
                extension != ".lrc")
                Fail("subtitle extension must be SRT, WebVTT, or LRC");
            const std::string subtitle_source = ReadSubtitle(subtitle_path);
            data.subtitles = ParseSubtitles(subtitle_source, extension, duration_frames);
            data.subtitle_language = request.options.subtitle_language.empty()
                                         ? "und" : request.options.subtitle_language;
        }
        else if (request.options.subtitle_language != "und")
        {
            Fail("--subtitle-language requires --subtitle");
        }
        try
        {
            CookedAudio cooked{};
            cooked.data = std::move(data);
            cooked.bytes = SerializeNativeAudio(cooked.data);
            ValidateNativeAudioProductStructure(cooked.bytes);
            cooked.product_hash = ComputeNativeAudioProductHash(cooked.bytes);
            return cooked;
        }
        catch (const NativeAudioError &exception)
        {
            Fail(std::string{"audio product validation failed: "} + exception.what());
        }
    }

    CookedAudio ImportAudio(const AudioImportRequest &request)
    {
        return AudioImporter{}.Import(request);
    }

    void PublishCookedAudioProduct(const std::filesystem::path &archive_root,
                                   const CookedAudio &cooked)
    {
        if (archive_root.empty() || cooked.bytes.empty() ||
            Sha256(cooked.bytes) != cooked.product_hash)
            Fail("audio product publication request is invalid");
        ValidateNativeAudioProductStructure(cooked.bytes);
        const auto product_path = archive_root /
            ProductRelativePath(ArchiveProductType::Audio, cooked.product_hash);
        std::error_code error;
        std::filesystem::create_directories(product_path.parent_path(), error);
        if (error) Fail("failed to create audio product directory");
        if (std::filesystem::exists(product_path))
        {
            const std::uint64_t existing_size = std::filesystem::file_size(product_path, error);
            if (!error && existing_size <= kNativeAudioMaxProductBytes &&
                existing_size == cooked.bytes.size())
            {
                const auto existing = ReadBounded(product_path, kNativeAudioMaxProductBytes,
                                                  "existing audio product");
                if (existing == cooked.bytes) return;
                if (Sha256(existing) == cooked.product_hash)
                    Fail("immutable audio product hash collision");
            }
            error.clear();
        }
        static std::atomic<std::uint64_t> temporary_sequence{0};
        auto temporary = product_path;
        temporary += std::filesystem::path{
            ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(temporary_sequence.fetch_add(1, std::memory_order_relaxed))};
        struct TemporaryCleanup
        {
            std::filesystem::path path;
            ~TemporaryCleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } cleanup{temporary};
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) Fail("failed to create staged audio product");
            output.write(reinterpret_cast<const char *>(cooked.bytes.data()),
                         static_cast<std::streamsize>(cooked.bytes.size()));
            output.flush();
            if (!output) Fail("failed to write staged audio product");
        }
#if defined(_WIN32)
        const BOOL published = MoveFileExW(temporary.c_str(), product_path.c_str(),
                                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (published == 0)
        {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            Fail("failed to atomically publish audio product");
        }
#else
        std::filesystem::rename(temporary, product_path, error);
        if (error)
        {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            Fail("failed to atomically publish audio product");
        }
#endif
    }
}
