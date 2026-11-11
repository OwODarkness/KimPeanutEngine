#include "audio_player_controller.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <complex>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <random>
#include <span>
#include <thread>
#include <unordered_set>
#include <string_view>

#include "runtime/asset/asset_manager.h"
#include "runtime/asset/asset_catalog_snapshot_provider.h"
#include "runtime/asset/asset_import_registry.h"
#include "runtime/asset/audio.h"
#include "runtime/asset/audio_import_service.h"
#include "runtime/asset/asset_product.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "runtime/audio/seekable_audio_player.h"
#include "runtime/core/config/path.h"
#include "log/logger.h"

namespace kpengine::audio_player
{
    namespace
    {
        constexpr std::uint64_t kMaximumFileBytes = 256ull * 1024ull * 1024ull;
        constexpr std::size_t kMaximumQueueTracks = 512;
        constexpr std::size_t kMaximumFolderFiles = 512;
        constexpr std::array<std::string_view, 3> kSupportedExtensions{
            ".wav", ".mp3", ".flac"};

        std::string PathUtf8(const std::filesystem::path &path)
        {
            const auto value = path.u8string();
            return {reinterpret_cast<const char *>(value.data()), value.size()};
        }

        std::filesystem::path FromUtf8(const std::string &path)
        {
            const auto *first = reinterpret_cast<const char8_t *>(path.data());
            return std::filesystem::path(std::u8string(first, first + path.size()));
        }

        std::string LowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            });
            return value;
        }

        std::string CanonicalKey(const std::filesystem::path &path)
        {
            return LowerAscii(PathUtf8(path.lexically_normal()));
        }

        std::string Extension(const std::filesystem::path &path)
        {
            return LowerAscii(PathUtf8(path.extension()));
        }

        bool IsSupported(const std::filesystem::path &path)
        {
            const std::string extension = Extension(path);
            return std::find(kSupportedExtensions.begin(), kSupportedExtensions.end(),
                             extension) != kSupportedExtensions.end();
        }

        std::shared_ptr<const std::vector<float>> BuildWaveform(const data::AudioClip &clip)
        {
            constexpr std::size_t kWaveformSampleCount = 8192;
            auto peaks = std::make_shared<std::vector<float>>(kWaveformSampleCount, 0.0f);
            const std::uint64_t channels = clip.format.channels;
            if (channels == 0 || clip.frame_count == 0 ||
                clip.pcm.size() < clip.frame_count * channels)
            {
                return peaks;
            }

            for (std::size_t bin = 0; bin < peaks->size(); ++bin)
            {
                const std::uint64_t first = clip.frame_count * bin / peaks->size();
                const std::uint64_t last = std::max(first + 1,
                    clip.frame_count * (bin + 1) / peaks->size());
                float peak = 0.0f;
                for (std::uint64_t frame = first; frame < std::min(last, clip.frame_count); ++frame)
                {
                    for (std::uint64_t channel = 0; channel < channels; ++channel)
                    {
                        const float sample = clip.pcm[static_cast<std::size_t>(frame * channels + channel)];
                        peak = std::max(peak, std::abs(sample));
                    }
                }
                (*peaks)[bin] = std::clamp(peak, 0.0f, 1.0f);
            }
            return peaks;
        }

        struct SignalSnapshot
        {
            float rms = 0.0f;
            float peak = 0.0f;
            std::array<float, 48> spectrum{};
        };

        SignalSnapshot SampleSignal(const std::span<const float> pcm,
                                    const std::size_t channels)
        {
            SignalSnapshot signal{};
            constexpr std::size_t count = 256;
            constexpr float pi = 3.14159265358979323846f;
            std::array<std::complex<float>, count> bins{};
            if (channels == 0 || pcm.empty())
                return signal;
            const std::size_t frame_count = std::min(count, pcm.size() / channels);
            const std::size_t first_sample = (pcm.size() / channels - frame_count) * channels;
            const std::size_t leading_silence = count - frame_count;

            float energy = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (i < leading_silence)
                    continue;
                const std::size_t source_frame = i - leading_silence;
                float sample = 0.0f;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    sample += pcm[first_sample + source_frame * channels + channel];
                sample /= static_cast<float>(channels);
                signal.peak = std::max(signal.peak, std::abs(sample));
                energy += sample * sample;
                const float window = 0.5f - 0.5f * std::cos(
                    2.0f * pi * static_cast<float>(i) / static_cast<float>(count - 1));
                bins[i] = {sample * window, 0.0f};
            }
            signal.rms = std::sqrt(energy / static_cast<float>(count));

            for (std::size_t i = 1, j = 0; i < count; ++i)
            {
                std::size_t bit = count >> 1;
                while (j & bit)
                {
                    j ^= bit;
                    bit >>= 1;
                }
                j ^= bit;
                if (i < j)
                    std::swap(bins[i], bins[j]);
            }
            for (std::size_t length = 2; length <= count; length <<= 1)
            {
                const float angle = -2.0f * pi / static_cast<float>(length);
                const std::complex<float> step{std::cos(angle), std::sin(angle)};
                for (std::size_t base = 0; base < count; base += length)
                {
                    std::complex<float> rotation{1.0f, 0.0f};
                    for (std::size_t offset = 0; offset < length / 2; ++offset)
                    {
                        const auto even = bins[base + offset];
                        const auto odd = bins[base + offset + length / 2] * rotation;
                        bins[base + offset] = even + odd;
                        bins[base + offset + length / 2] = even - odd;
                        rotation *= step;
                    }
                }
            }
            for (std::size_t i = 0; i < signal.spectrum.size(); ++i)
            {
                const float ratio = static_cast<float>(i) /
                    static_cast<float>(signal.spectrum.size() - 1);
                const std::size_t bin = static_cast<std::size_t>(
                    std::round(std::pow(120.0f, ratio)));
                signal.spectrum[i] = std::clamp(
                    std::abs(bins[std::clamp<std::size_t>(bin, 1, count / 2)]) *
                        (5.0f / static_cast<float>(count)), 0.0f, 1.0f);
            }
            return signal;
        }

        std::string StateName(const audio::AudioState state)
        {
            switch (state)
            {
            case audio::AudioState::Playing: return "Playing";
            case audio::AudioState::Buffering: return "Buffering";
            case audio::AudioState::FadingOut: return "Fading out";
            case audio::AudioState::Paused: return "Paused";
            case audio::AudioState::Stopped: return "Ready";
            case audio::AudioState::Finished: return "Finished";
            case audio::AudioState::Cancelled: return "Cancelled";
            }
            return "Unknown";
        }
    }

    struct AudioPlayerController::Impl
    {
        struct Track
        {
            TrackView view;
            std::shared_ptr<const data::AudioClip> clip;
            std::shared_ptr<const asset::NativeAudioFileProduct> native_product;
        };

        struct ImportJob
        {
            std::filesystem::path path;
            std::filesystem::path subtitle_path;
            std::uint64_t file_size = 0;
            std::string content_id;
            std::filesystem::path product_path;
            std::string name;
            bool project_asset = false;
        };

        struct Voice
        {
            audio::AudioHandle handle{};
            std::shared_ptr<audio::AudioPlayer> player;
            std::shared_ptr<audio::BufferAudioPlayer> buffer_player;
            std::shared_ptr<const data::AudioClip> clip;
            std::shared_ptr<const asset::NativeAudioFileProduct> native_product;
            TrackView track_snapshot;
            std::uint64_t track_id = 0;
        };

        SignalSnapshot CurrentSignal()
        {
            if (!voice.player || (!voice.clip && !voice.native_product) ||
                !audio_system->IsInitialized() ||
                voice.player->GetCurrentState() != audio::AudioState::Playing ||
                muted || audio_system->IsBusMuted(audio::AudioBus::Music) || volume <= 0.0f)
            {
                signal_cache = {};
                return signal_cache;
            }
            const auto now = std::chrono::steady_clock::now();
            const std::uint64_t frame = voice.player->GetPlayedFrameCursor();
            if (voice.track_id != signal_track_id ||
                frame < signal_frame ||
                now - signal_updated >= std::chrono::milliseconds(33))
            {
                if (voice.clip != nullptr)
                {
                    const std::size_t channels = voice.clip->format.channels;
                    const std::uint64_t frames_before_cursor = std::min<std::uint64_t>(
                        frame, voice.clip->frame_count);
                    const std::uint64_t first_frame = frames_before_cursor > 256
                        ? frames_before_cursor - 256 : 0;
                    const std::size_t available_samples = static_cast<std::size_t>(
                        frames_before_cursor - first_frame) * channels;
                    if (channels != 0 && available_samples <= voice.clip->pcm.size())
                    {
                        signal_cache = SampleSignal(
                            std::span<const float>{voice.clip->pcm.data() +
                                static_cast<std::size_t>(first_frame) * channels,
                                available_samples},
                            channels);
                    }
                }
                else if (auto seekable =
                             std::dynamic_pointer_cast<audio::SeekableAudioPlayer>(voice.player))
                {
                    constexpr std::uint32_t window_frames = 256;
                    std::array<float, window_frames * 2> samples{};
                    std::uint32_t channels = 0;
                    const std::uint32_t copied = seekable->CopyBufferedFrames(
                        frame, samples.data(), window_frames, channels);
                    if (copied == 0)
                        return signal_cache;
                    signal_cache = SampleSignal(
                        std::span<const float>{samples.data(),
                            static_cast<std::size_t>(copied) * channels}, channels);
                }
                else
                {
                    signal_cache = {};
                }
                const float gain = muted || audio_system->IsBusMuted(audio::AudioBus::Music)
                    ? 0.0f : volume * audio_system->GetBusGain(audio::AudioBus::Music) *
                        audio_system->GetMasterGain();
                signal_cache.rms *= gain;
                signal_cache.peak *= gain;
                for (float &bin : signal_cache.spectrum)
                    bin *= gain;
                signal_frame = frame;
                signal_track_id = voice.track_id;
                signal_updated = now;
            }
            return signal_cache;
        }

        explicit Impl(audio::MiniAudioSystem &in_audio_system)
            : audio_system(&in_audio_system), import_thread([this] { RunImports(); })
        {
        }

        bool RequestProjectLibraryRefresh(std::string &diagnostic)
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                diagnostic = "Audio Player is shutting down";
                return false;
            }
            project_refresh_requested = true;
            project_library.refreshing = true;
            project_library.error.clear();
            project_library.status = "Scanning project Content";
            import_changed.notify_one();
            diagnostic.clear();
            return true;
        }

        bool EnqueueProjectTrack(std::string content_id, std::string &diagnostic)
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                diagnostic = "Audio Player is shutting down";
                return false;
            }
            const auto found = std::find_if(project_library.tracks.begin(),
                project_library.tracks.end(), [&content_id](const TrackView &track)
                {
                    return track.content_id == content_id;
                });
            if (found == project_library.tracks.end())
            {
                diagnostic = "Project audio entry is no longer available; refresh the Library";
                return false;
            }
            if (tracks.size() + import_jobs.size() + (import_active ? 1 : 0) >=
                kMaximumQueueTracks)
            {
                diagnostic = "Queue limit reached (512 tracks)";
                last_error = diagnostic;
                return false;
            }
            const std::string key = "content:" + content_id;
            if (!queued_paths.insert(key).second)
            {
                diagnostic = "This project track is already being queued";
                return false;
            }
            const std::filesystem::path asset_source =
                FromUtf8(PathUtf8(GetAssetDirectory())) / FromUtf8(found->path);
            import_jobs.push_back({asset_source, {}, found->file_size, content_id,
                                   FromUtf8(found->product_path), found->name, true});
            last_status = "Loading project track: " + found->name;
            last_error.clear();
            import_changed.notify_one();
            diagnostic.clear();
            return true;
        }

        ~Impl()
        {
            Shutdown();
        }

        void SetImportError(const std::string &message)
        {
            std::lock_guard lock(mutex);
            last_status = "Import rejected";
            last_error = message;
        }

        bool EnqueueImport(const std::filesystem::path &path,
                           const std::filesystem::path &subtitle_path,
                           std::string &diagnostic)
        {
            std::error_code error;
            const std::filesystem::path absolute_path =
                std::filesystem::absolute(path, error).lexically_normal();
            if (error)
            {
                diagnostic = "Could not resolve the selected audio path";
                SetImportError(diagnostic);
                return false;
            }
            const std::filesystem::path absolute_subtitle = subtitle_path.empty()
                ? std::filesystem::path{}
                : std::filesystem::absolute(subtitle_path, error).lexically_normal();
            if (error)
            {
                diagnostic = "Could not resolve the selected subtitle path";
                SetImportError(diagnostic);
                return false;
            }
            if (!std::filesystem::is_regular_file(absolute_path, error) || error)
            {
                diagnostic = "File does not exist or is not a regular file";
                SetImportError(diagnostic);
                return false;
            }
            if (!IsSupported(absolute_path))
            {
                diagnostic = "Supported formats are WAV, MP3, and FLAC";
                SetImportError(diagnostic);
                return false;
            }
            const std::uint64_t size = std::filesystem::file_size(absolute_path, error);
            if (error || size == 0)
            {
                diagnostic = error ? "Could not read the file size" : "Audio file is empty";
                SetImportError(diagnostic);
                return false;
            }
            if (size > kMaximumFileBytes)
            {
                diagnostic = "Audio file exceeds the 256 MiB import limit";
                SetImportError(diagnostic);
                return false;
            }

            const std::string key = CanonicalKey(absolute_path);
            std::lock_guard lock(mutex);
            if (stopping)
            {
                diagnostic = "Audio Player is shutting down";
                return false;
            }
            if (tracks.size() + import_jobs.size() + (import_active ? 1 : 0) >=
                kMaximumQueueTracks)
            {
                diagnostic = "Queue limit reached (512 tracks)";
                last_error = diagnostic;
                return false;
            }
            if (queued_paths.contains(key))
            {
                diagnostic = "This file is already being imported";
                last_error = diagnostic;
                return false;
            }

            if (!absolute_subtitle.empty())
            {
                error.clear();
                if (!std::filesystem::is_regular_file(absolute_subtitle, error) || error)
                {
                    diagnostic = "Subtitle does not exist or is not a regular file";
                    last_error = diagnostic;
                    return false;
                }
                const std::string subtitle_extension = Extension(absolute_subtitle);
                if (subtitle_extension != ".srt" && subtitle_extension != ".vtt" &&
                    subtitle_extension != ".lrc")
                {
                    diagnostic = "Subtitle format must be SRT, WebVTT, or LRC";
                    last_error = diagnostic;
                    return false;
                }
            }

            import_jobs.push_back({absolute_path, absolute_subtitle, size});
            queued_paths.insert(key);
            last_status = "Import queued: " + PathUtf8(absolute_path.filename());
            last_error.clear();
            import_changed.notify_one();
            diagnostic.clear();
            return true;
        }

        ProjectLibraryView CaptureProjectLibrary()
        {
            ProjectLibraryView result;
            try
            {
                asset::AssetCatalogSnapshotProvider provider(
                    asset::AssetManager::GetInstance(), {});
                const asset::AssetCatalogSnapshot snapshot = provider.CaptureAssetCatalog();
                if (!snapshot.diagnostics.empty())
                    result.error = snapshot.diagnostics.front().message;

                for (const asset::AssetCatalogNode &node : snapshot.nodes)
                {
                    if (node.kind != asset::AssetCatalogNodeKind::Asset ||
                        node.type != asset::AssetType::KPAT_Audio ||
                        node.archive_product_type != asset::ArchiveProductType::Audio ||
                        node.content_id.empty() || node.product_path.empty())
                    {
                        continue;
                    }

                    if (node.provenance.empty() || node.provenance.front().source_path.empty())
                    {
                        if (result.error.empty())
                            result.error = "Project audio has no Asset source path: " +
                                           node.logical_path;
                        continue;
                    }
                    asset::ImportProviderRequest status_request{};
                    status_request.asset_root = FromUtf8(GetAssetDirectory());
                    status_request.archive_root = FromUtf8(GetContentArchiveDirectory());
                    status_request.source_path =
                        FromUtf8(node.provenance.front().source_path);
                    const asset::AudioArchiveStatus published =
                        asset::AudioImportService{}.Status(status_request);
                    if (published.status != asset::ArchiveProbeStatus::UpToDate ||
                        !node.content_hash || *node.content_hash != published.product_hash ||
                        FromUtf8(node.product_path).lexically_normal() !=
                            published.product_path.lexically_normal())
                    {
                        if (result.error.empty())
                        {
                            result.error = "Project audio is stale or does not match its published "
                                "product: " + node.logical_path;
                            if (!published.diagnostic.empty())
                                result.error += " (" + published.diagnostic + ")";
                        }
                        continue;
                    }

                    const asset::AssetID asset_id =
                        asset::AssetManager::GetInstance().LoadSync(node.product_path);
                    const auto resource = asset_id.IsValid()
                        ? asset::AssetManager::GetInstance().GetResource<asset::AudioResource>(asset_id)
                        : nullptr;
                    if (resource == nullptr || resource->native_product == nullptr ||
                        resource->native_product->metadata.duration_frames == 0 ||
                        resource->native_product->encoded_audio_size == 0)
                    {
                        if (result.error.empty())
                            result.error = "Could not validate project audio product: " +
                                           node.logical_path;
                        continue;
                    }

                    TrackView track{};
                    track.name = node.display_name;
                    track.path = node.provenance.empty()
                        ? node.logical_path : node.provenance.front().source_path;
                    track.extension = Extension(FromUtf8(track.path));
                    track.content_id = node.content_id;
                    track.product_path = node.product_path;
                    track.file_size = node.byte_size;
                    track.duration_seconds = static_cast<float>(
                        resource->native_product->metadata.duration_frames) /
                        static_cast<float>(asset::kNativeAudioTimelineSampleRate);
                    track.sample_rate = resource->native_product->metadata.sample_rate;
                    track.channels = static_cast<std::uint16_t>(
                        resource->native_product->metadata.channels);
                    track.native_product = true;
                    track.project_asset = true;
                    track.has_subtitles = !resource->native_product->metadata.subtitle_language.empty();
                    track.subtitle_cue_count = static_cast<std::uint32_t>(
                        resource->native_product->metadata.subtitles.size());
                    track.subtitle_language =
                        resource->native_product->metadata.subtitle_language;
                    auto peaks = std::make_shared<std::vector<float>>();
                    peaks->reserve(resource->native_product->metadata.waveform.size());
                    for (const asset::NativeAudioPeak &peak :
                         resource->native_product->metadata.waveform)
                    {
                        peaks->push_back(std::max(std::abs(peak.minimum),
                                                  std::abs(peak.maximum)));
                    }
                    track.waveform = std::move(peaks);
                    result.tracks.push_back(std::move(track));
                }
                result.status = result.tracks.empty()
                    ? "No project audio found" :
                      "Found " + std::to_string(result.tracks.size()) + " project tracks";
            }
            catch (const std::exception &exception)
            {
                result.error = exception.what();
                result.status = "Project Library refresh failed";
            }
            result.refreshing = false;
            return result;
        }

        void RunImports()
        {
            for (;;)
            {
                ImportJob job;
                bool refresh_project_library = false;
                {
                    std::unique_lock lock(mutex);
                    import_changed.wait(lock, [this]
                    {
                        return stopping || project_refresh_requested || !import_jobs.empty();
                    });
                    if (stopping && import_jobs.empty())
                    {
                        return;
                    }
                    if (project_refresh_requested && !stopping)
                    {
                        project_refresh_requested = false;
                        refresh_project_library = true;
                    }
                    else
                    {
                        job = std::move(import_jobs.front());
                        import_jobs.pop_front();
                        import_active = true;
                        last_status = job.project_asset
                            ? "Loading project track: " + job.name
                            : "Decoding: " + PathUtf8(job.path.filename());
                    }
                }

                if (refresh_project_library)
                {
                    ProjectLibraryView refreshed = CaptureProjectLibrary();
                    std::lock_guard lock(mutex);
                    project_library = std::move(refreshed);
                    continue;
                }

                std::string failure;
                const std::string path = PathUtf8(job.path);
                std::shared_ptr<asset::AudioResource> resource;
                try
                {
                    std::filesystem::path product_path = job.product_path;
                    if (!job.project_asset)
                    {
                        std::error_code path_error;
                        const std::filesystem::path canonical_source =
                            std::filesystem::weakly_canonical(job.path, path_error);
                        if (path_error)
                            throw std::runtime_error("Could not resolve the selected audio path");

                        asset::ImportProviderRequest request{};
                        request.asset_root = job.path.parent_path();
                        request.archive_root = std::filesystem::path(GetSaveDirectory()) /
                            "audio_player" /
                            asset::Sha256(PathUtf8(canonical_source)).ToHex();
                        request.source_path = job.path.filename();
                        if (!job.subtitle_path.empty())
                        {
                            request.audio_options = asset::AudioImportOptions{};
                            const std::filesystem::path relative_subtitle =
                                job.subtitle_path.lexically_relative(request.asset_root);
                            if (relative_subtitle.empty() || relative_subtitle.is_absolute())
                                throw std::runtime_error(
                                    "Subtitle must be inside the audio file's folder");
                            request.audio_options->subtitle_path = relative_subtitle;
                        }

                        product_path = asset::AudioImportService{}.Import(request).product_path;
                    }

                    const asset::AssetID asset_id = asset::AssetManager::GetInstance().LoadSync(
                        product_path.generic_string());
                    resource = asset_id.IsValid()
                        ? asset::AssetManager::GetInstance().GetResource<asset::AudioResource>(asset_id)
                        : nullptr;
                }
                catch (const std::exception &exception)
                {
                    failure = exception.what();
                }
                if (resource == nullptr || resource->native_product == nullptr ||
                    resource->native_product->metadata.duration_frames == 0 ||
                    resource->native_product->encoded_audio_size == 0)
                {
                    if (failure.empty())
                        failure = "Could not load the native Audio product";
                }

                std::lock_guard lock(mutex);
                import_active = false;
                const std::string key = job.project_asset
                    ? "content:" + job.content_id : CanonicalKey(job.path);
                queued_paths.erase(key);
                const auto existing_track = std::find_if(
                    tracks.begin(), tracks.end(), [&job, &key](const Track &candidate)
                    {
                        return job.project_asset
                            ? candidate.view.content_id == job.content_id
                            : CanonicalKey(FromUtf8(candidate.view.path)) == key;
                    });
                if (failure.empty() &&
                    (tracks.size() < kMaximumQueueTracks || existing_track != tracks.end()))
                {
                    Track track{};
                    track.view.id = existing_track == tracks.end()
                        ? next_track_id++ : existing_track->view.id;
                    track.view.favorite = existing_track != tracks.end() &&
                        existing_track->view.favorite;
                    track.view.path = path;
                    track.view.name = job.name.empty() ? PathUtf8(job.path.stem()) : job.name;
                    track.view.extension = Extension(job.path);
                    track.view.file_size = job.file_size;
                    track.view.content_id = job.content_id;
                    track.view.project_asset = job.project_asset;
                    track.view.product_path = PathUtf8(resource->native_product->path);
                    const asset::NativeAudioFileProduct &native = *resource->native_product;
                    track.view.duration_seconds = static_cast<float>(native.metadata.duration_frames) /
                        static_cast<float>(asset::kNativeAudioTimelineSampleRate);
                    track.view.sample_rate = native.metadata.sample_rate;
                    track.view.channels = static_cast<std::uint16_t>(native.metadata.channels);
                    track.view.native_product = true;
                    track.view.has_subtitles = !native.metadata.subtitle_language.empty();
                    track.view.subtitle_cue_count =
                        static_cast<std::uint32_t>(native.metadata.subtitles.size());
                    track.view.subtitle_language = native.metadata.subtitle_language;
                    auto peaks = std::make_shared<std::vector<float>>();
                    peaks->reserve(native.metadata.waveform.size());
                    for (const asset::NativeAudioPeak &peak : native.metadata.waveform)
                        peaks->push_back(std::max(std::abs(peak.minimum), std::abs(peak.maximum)));
                    track.view.waveform = std::move(peaks);
                    track.native_product = resource->native_product;
                    const bool reimported = existing_track != tracks.end();
                    if (!selected_track_id.has_value())
                    {
                        selected_track_id = track.view.id;
                    }
                    last_status = std::string(reimported ? "Reimported: " : "Imported: ") +
                        track.view.name;
                    if (reimported)
                    {
                        *existing_track = std::move(track);
                    }
                    else
                    {
                        track_paths.insert(CanonicalKey(FromUtf8(track.view.path)));
                        tracks.push_back(std::move(track));
                    }
                    if (!voice.handle.IsValid() && selected_track_id.has_value())
                    {
                        if (Track *selected = FindTrack(*selected_track_id); selected != nullptr)
                        {
                            std::string ignored;
                            PrepareVoice(*selected, ignored);
                        }
                    }
                    KP_LOG("Audio", LOG_LEVEL_INFO, "Imported audio file: %s", path.c_str());
                }
                else
                {
                    last_error = failure.empty() ? "Queue limit reached during import" : failure;
                    last_status = (job.project_asset ? "Project track load failed: "
                                                     : "Import failed: ") +
                        (job.name.empty() ? PathUtf8(job.path.filename()) : job.name);
                    KP_LOG("Audio", LOG_LEVEL_WARNING, "Audio import failed for %s: %s",
                           path.c_str(), last_error.c_str());
                }
            }
        }

        Track *FindTrack(std::uint64_t id)
        {
            const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const Track &track) {
                return track.view.id == id;
            });
            return found == tracks.end() ? nullptr : &*found;
        }

        const Track *FindTrack(std::uint64_t id) const
        {
            const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const Track &track) {
                return track.view.id == id;
            });
            return found == tracks.end() ? nullptr : &*found;
        }

        void DestroyVoice() noexcept
        {
            if (voice.handle.IsValid())
            {
                voice.player->Stop();
                audio_system->DestroyAudioPlayer(voice.handle);
            }
            voice = {};
        }

        bool PrepareVoice(Track &track, std::string &diagnostic)
        {
            if (voice.handle.IsValid() && voice.track_id == track.view.id)
            {
                const bool same_native_product = voice.native_product == track.native_product;
                const bool same_clip = voice.clip == track.clip;
                if (same_native_product && same_clip)
                {
                    diagnostic.clear();
                    return true;
                }
            }
            DestroyVoice();
            const audio::AudioPlayerType player_type = track.native_product != nullptr
                ? audio::AudioPlayerType::Seekable : audio::AudioPlayerType::Buffer;
            const audio::AudioHandle handle = audio_system->CreateAudioPlayer(player_type);
            auto player = audio_system->GetAudioPlayer(handle);
            auto buffer = player_type == audio::AudioPlayerType::Buffer
                ? std::dynamic_pointer_cast<audio::BufferAudioPlayer>(player) : nullptr;
            auto seekable = player_type == audio::AudioPlayerType::Seekable
                ? std::dynamic_pointer_cast<audio::SeekableAudioPlayer>(player) : nullptr;
            if (!handle.IsValid() || player == nullptr ||
                (player_type == audio::AudioPlayerType::Buffer && buffer == nullptr) ||
                (player_type == audio::AudioPlayerType::Seekable && seekable == nullptr))
            {
                if (handle.IsValid())
                {
                    audio_system->DestroyAudioPlayer(handle);
                }
                diagnostic = "Audio voice capacity is exhausted";
                last_error = diagnostic;
                last_status = "Could not create playback voice";
                return false;
            }
            if (buffer != nullptr)
            {
                buffer->SetClip(track.clip);
            }
            else
            {
                const asset::NativeAudioFileProduct &native = *track.native_product;
                const audio::FileBackedAudioSource source{
                    native.path, native.encoded_audio_offset, native.encoded_audio_size,
                    native.metadata.duration_frames, track.native_product};
                if (!seekable->SetSource(source))
                {
                    diagnostic = seekable->GetDiagnostic();
                    audio_system->DestroyAudioPlayer(handle);
                    last_error = diagnostic;
                    last_status = "Could not prepare playback decoder";
                    return false;
                }
            }
            player->SetBus(audio::AudioBus::Music);
            player->SetVolume(muted ? 0.0f : volume);
            player->SetShouldLoop(loop_track);
            if (buffer != nullptr)
                buffer->SetPlaybackRate(playback_rate);
            if (seekable != nullptr)
                seekable->SetPlaybackRate(playback_rate);
            voice = {handle, std::move(player), std::move(buffer), track.clip,
                     track.native_product, track.view, track.view.id};
            selected_track_id = track.view.id;
            diagnostic.clear();
            return true;
        }

        bool StartTrack(Track &track, std::string &diagnostic)
        {
            if (!PrepareVoice(track, diagnostic))
            {
                return false;
            }
            if (!audio_system->IsInitialized() && !audio_system->Initialize())
            {
                diagnostic = "Output device could not be initialized; retry from the device panel";
                last_error = diagnostic;
                last_status = "Output device unavailable";
                return false;
            }

            const audio::AudioState state = voice.player->GetCurrentState();
            if (state == audio::AudioState::Finished || state == audio::AudioState::Stopped)
            {
                voice.player->Restart();
            }
            else
            {
                voice.player->Play();
            }
            last_error.clear();
            last_status = "Playing: " + track.view.name;
            diagnostic.clear();
            return true;
        }

        bool Step(int direction, bool play, std::string &diagnostic)
        {
            if (tracks.empty())
            {
                diagnostic = "Queue is empty";
                return false;
            }
            std::size_t current = 0;
            if (selected_track_id.has_value())
            {
                const auto found = std::find_if(tracks.begin(), tracks.end(), [this](const Track &track) {
                    return track.view.id == *selected_track_id;
                });
                if (found != tracks.end())
                {
                    current = static_cast<std::size_t>(std::distance(tracks.begin(), found));
                }
            }

            std::size_t next = current;
            if (shuffle && tracks.size() > 1 && direction > 0)
            {
                std::uniform_int_distribution<std::size_t> distribution(0, tracks.size() - 2);
                next = distribution(random);
                if (next >= current)
                {
                    ++next;
                }
            }
            else
            {
                const auto count = static_cast<std::int64_t>(tracks.size());
                const auto candidate = (static_cast<std::int64_t>(current) + direction + count) % count;
                next = static_cast<std::size_t>(candidate);
            }

            selected_track_id = tracks[next].view.id;
            if (play)
            {
                return StartTrack(tracks[next], diagnostic);
            }
            if (voice.handle.IsValid() && voice.track_id != tracks[next].view.id)
            {
                DestroyVoice();
            }
            PrepareVoice(tracks[next], diagnostic);
            last_status = "Selected: " + tracks[next].view.name;
            diagnostic.clear();
            return true;
        }

        void Shutdown() noexcept
        {
            {
                std::lock_guard lock(mutex);
                if (stopping)
                {
                    return;
                }
                stopping = true;
            }
            import_changed.notify_all();
            if (import_thread.joinable())
            {
                import_thread.join();
            }
            std::lock_guard lock(mutex);
            DestroyVoice();
            tracks.clear();
            project_library.tracks.clear();
            import_jobs.clear();
            queued_paths.clear();
            track_paths.clear();
        }

        audio::MiniAudioSystem *audio_system;
        mutable std::mutex mutex;
        std::condition_variable import_changed;
        std::deque<ImportJob> import_jobs;
        std::vector<Track> tracks;
        ProjectLibraryView project_library;
        std::unordered_set<std::string> queued_paths;
        std::unordered_set<std::string> track_paths;
        std::mt19937 random{std::random_device{}()};
        std::optional<std::uint64_t> selected_track_id;
        Voice voice;
        SignalSnapshot signal_cache{};
        std::chrono::steady_clock::time_point signal_updated{};
        std::uint64_t signal_frame = 0;
        std::uint64_t signal_track_id = 0;
        std::uint64_t next_track_id = 1;
        float volume = 0.8f;
        float playback_rate = 1.0f;
        bool muted = false;
        bool loop_track = false;
        bool shuffle = false;
        bool import_active = false;
        bool project_refresh_requested = false;
        bool stopping = false;
        bool finished_handled = false;
        std::string last_status = "Ready — import audio to begin";
        std::string last_error;
        std::thread import_thread;
    };

    AudioPlayerController::AudioPlayerController(audio::MiniAudioSystem &audio_system)
        : impl_(std::make_unique<Impl>(audio_system))
    {
    }

    AudioPlayerController::~AudioPlayerController() = default;

    bool AudioPlayerController::ImportFile(std::string path, std::string &diagnostic)
    {
        return ImportFile(std::move(path), {}, diagnostic);
    }

    bool AudioPlayerController::ImportFile(std::string path, std::string subtitle_path,
                                           std::string &diagnostic)
    {
        return impl_->EnqueueImport(FromUtf8(path),
            subtitle_path.empty() ? std::filesystem::path{} : FromUtf8(subtitle_path),
            diagnostic);
    }

    bool AudioPlayerController::ReimportSelected(std::string subtitle_path,
                                                 std::string &diagnostic)
    {
        std::filesystem::path source;
        {
            std::lock_guard lock(impl_->mutex);
            if (!impl_->selected_track_id.has_value())
            {
                diagnostic = "Select a track before reimporting";
                return false;
            }
            const Impl::Track *const track = impl_->FindTrack(*impl_->selected_track_id);
            if (track == nullptr)
            {
                diagnostic = "The selected track is no longer in the queue";
                return false;
            }
            if (track->view.project_asset)
            {
                diagnostic = "Project audio must be reimported through KimPeanutAssetTool";
                return false;
            }
            source = FromUtf8(track->view.path);
        }
        return ImportFile(PathUtf8(source), std::move(subtitle_path), diagnostic);
    }

    bool AudioPlayerController::ImportFolder(std::string path, std::string &diagnostic)
    {
        const std::filesystem::path folder = FromUtf8(path);
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error) || error)
        {
            diagnostic = "Folder does not exist or cannot be opened";
            return false;
        }

        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(folder, error), end;
             !error && it != end && files.size() < kMaximumFolderFiles; it.increment(error))
        {
            if (it->is_regular_file(error) && !error && IsSupported(it->path()))
            {
                files.push_back(it->path());
            }
            error.clear();
        }
        std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) {
            return LowerAscii(PathUtf8(left.filename())) < LowerAscii(PathUtf8(right.filename()));
        });
        if (files.empty())
        {
            diagnostic = error ? "Could not enumerate the selected folder"
                               : "No WAV, MP3, or FLAC files were found";
            return false;
        }

        std::size_t accepted = 0;
        std::string last_failure;
        for (const auto &file : files)
        {
            std::string file_diagnostic;
            if (impl_->EnqueueImport(file, {}, file_diagnostic))
            {
                ++accepted;
            }
            else
            {
                last_failure = std::move(file_diagnostic);
            }
        }
        if (accepted == 0)
        {
            diagnostic = last_failure.empty() ? "No files were added to the queue" : last_failure;
            return false;
        }
        diagnostic.clear();
        return true;
    }

    std::vector<TrackView> AudioPlayerController::GetQueue() const
    {
        std::lock_guard lock(impl_->mutex);
        std::vector<TrackView> result;
        result.reserve(impl_->tracks.size());
        for (const auto &track : impl_->tracks)
        {
            result.push_back(track.view);
        }
        return result;
    }

    ProjectLibraryView AudioPlayerController::GetProjectLibrary() const
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->project_library;
    }

    bool AudioPlayerController::RefreshProjectLibrary(std::string &diagnostic)
    {
        return impl_->RequestProjectLibraryRefresh(diagnostic);
    }

    bool AudioPlayerController::QueueProjectTrack(std::string content_id,
                                                  std::string &diagnostic)
    {
        return impl_->EnqueueProjectTrack(std::move(content_id), diagnostic);
    }

    PlaybackView AudioPlayerController::GetPlaybackView() const
    {
        std::lock_guard lock(impl_->mutex);
        PlaybackView result{};
        if (impl_->selected_track_id.has_value())
        {
            if (const auto *track = impl_->FindTrack(*impl_->selected_track_id))
            {
                const bool voice_is_audible = impl_->voice.player != nullptr &&
                    (impl_->voice.player->GetCurrentState() == audio::AudioState::Playing ||
                     impl_->voice.player->GetCurrentState() == audio::AudioState::Buffering ||
                     impl_->voice.player->GetCurrentState() == audio::AudioState::Paused);
                const TrackView &display_track = voice_is_audible &&
                    impl_->voice.track_id == track->view.id &&
                    impl_->voice.native_product != track->native_product
                        ? impl_->voice.track_snapshot : track->view;
                result.track = display_track;
                result.duration_seconds = display_track.duration_seconds;
            }
        }
        if (impl_->voice.handle.IsValid() && impl_->voice.player != nullptr)
        {
            result.state = impl_->voice.player->GetCurrentState();
            result.position_seconds = impl_->voice.player->GetCurrentSecond();
            result.volume = impl_->volume;
            result.playback_rate = impl_->voice.player->GetPlaybackRate();
            result.muted = impl_->muted;
            result.can_seek = true;
            const Impl::Track *const selected_track = impl_->selected_track_id.has_value()
                ? impl_->FindTrack(*impl_->selected_track_id) : nullptr;
            const audio::AudioState voice_state = impl_->voice.player->GetCurrentState();
            const bool voice_is_active = voice_state == audio::AudioState::Playing ||
                voice_state == audio::AudioState::Buffering ||
                voice_state == audio::AudioState::Paused;
            const bool voice_matches_selected = selected_track != nullptr &&
                impl_->voice.track_id == selected_track->view.id;
            const bool voice_is_selected_product = voice_matches_selected &&
                impl_->voice.native_product != nullptr &&
                impl_->voice.native_product == selected_track->native_product;
            const bool voice_should_supply_cues = voice_matches_selected &&
                (voice_is_active || voice_is_selected_product);
            if (voice_matches_selected && voice_should_supply_cues &&
                impl_->voice.native_product != nullptr)
            {
                result.subtitle_language = impl_->voice.native_product->metadata.subtitle_language;
                result.subtitle_track_attached = !result.subtitle_language.empty();
                const auto active = impl_->voice.native_product->SubtitleTextAt(
                    impl_->voice.player->GetPlayedFrameCursor());
                for (const std::string_view text : active)
                {
                    if (!result.subtitle_text.empty())
                        result.subtitle_text.push_back('\n');
                    result.subtitle_text.append(text);
                }
            }
        }
        else
        {
            result.volume = impl_->volume;
            result.playback_rate = impl_->playback_rate;
            result.muted = impl_->muted;
        }
        result.loop_track = impl_->loop_track;
        result.shuffle = impl_->shuffle;
        result.status = impl_->last_status;
        result.error = impl_->last_error;
        result.pending_imports = impl_->import_jobs.size() + (impl_->import_active ? 1 : 0);
        result.queue_size = impl_->tracks.size();
        const SignalSnapshot signal = impl_->CurrentSignal();
        result.rms = signal.rms;
        result.peak = signal.peak;
        result.spectrum = signal.spectrum;
        return result;
    }

    bool AudioPlayerController::Select(const std::uint64_t track_id)
    {
        std::lock_guard lock(impl_->mutex);
        const auto *track = impl_->FindTrack(track_id);
        if (track == nullptr)
        {
            return false;
        }
        if (impl_->voice.handle.IsValid() && impl_->voice.track_id != track_id)
        {
            impl_->DestroyVoice();
        }
        impl_->selected_track_id = track_id;
        impl_->last_status = "Selected: " + track->view.name;
        std::string diagnostic;
        return impl_->PrepareVoice(*impl_->FindTrack(track_id), diagnostic);
    }

    bool AudioPlayerController::ToggleFavorite(const std::uint64_t track_id)
    {
        std::lock_guard lock(impl_->mutex);
        auto *track = impl_->FindTrack(track_id);
        if (track == nullptr)
        {
            return false;
        }
        track->view.favorite = !track->view.favorite;
        return track->view.favorite;
    }

    bool AudioPlayerController::RemoveSelected()
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->selected_track_id.has_value())
        {
            return false;
        }
        const std::uint64_t removed_id = *impl_->selected_track_id;
        const auto found = std::find_if(impl_->tracks.begin(), impl_->tracks.end(), [removed_id](const auto &track) {
            return track.view.id == removed_id;
        });
        if (found == impl_->tracks.end())
        {
            return false;
        }
        if (impl_->voice.track_id == removed_id)
        {
            impl_->DestroyVoice();
        }
        impl_->track_paths.erase(CanonicalKey(FromUtf8(found->view.path)));
        const std::size_t index = static_cast<std::size_t>(std::distance(impl_->tracks.begin(), found));
        impl_->tracks.erase(found);
        if (impl_->tracks.empty())
        {
            impl_->selected_track_id.reset();
        }
        else
        {
            impl_->selected_track_id = impl_->tracks[std::min(index, impl_->tracks.size() - 1)].view.id;
            std::string diagnostic;
            impl_->PrepareVoice(impl_->tracks[std::min(index, impl_->tracks.size() - 1)], diagnostic);
        }
        impl_->last_status = "Removed selected track";
        return true;
    }

    void AudioPlayerController::ClearQueue()
    {
        std::lock_guard lock(impl_->mutex);
        impl_->DestroyVoice();
        impl_->tracks.clear();
        impl_->track_paths.clear();
        impl_->selected_track_id.reset();
        impl_->last_status = "Queue cleared";
    }

    bool AudioPlayerController::PlaySelected(std::string &diagnostic)
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->selected_track_id.has_value())
        {
            diagnostic = "Select or import a track first";
            return false;
        }
        if (impl_->voice.handle.IsValid() &&
            impl_->voice.track_id == *impl_->selected_track_id)
        {
            const audio::AudioState state = impl_->voice.player->GetCurrentState();
            if (state == audio::AudioState::Paused)
            {
                impl_->voice.player->Play();
                impl_->last_status = "Playing: " + impl_->FindTrack(*impl_->selected_track_id)->view.name;
                diagnostic.clear();
                return true;
            }
            if (state == audio::AudioState::Playing || state == audio::AudioState::Buffering)
            {
                diagnostic.clear();
                return true;
            }
        }
        auto *track = impl_->FindTrack(*impl_->selected_track_id);
        if (track == nullptr)
        {
            diagnostic = "Selected track is no longer in the queue";
            return false;
        }
        return impl_->StartTrack(*track, diagnostic);
    }

    bool AudioPlayerController::TogglePlayPause(std::string &diagnostic)
    {
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->voice.handle.IsValid())
            {
                const audio::AudioState state = impl_->voice.player->GetCurrentState();
                if (state == audio::AudioState::Playing || state == audio::AudioState::Buffering)
                {
                    impl_->voice.player->Pause();
                    impl_->last_status = "Paused";
                    diagnostic.clear();
                    return true;
                }
            }
        }
        return PlaySelected(diagnostic);
    }

    void AudioPlayerController::Pause()
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.player->Pause();
            impl_->last_status = "Paused";
        }
    }

    void AudioPlayerController::Stop()
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.player->Stop();
        }
        impl_->last_status = impl_->selected_track_id.has_value() ? "Stopped" : "Ready — import audio to begin";
    }

    bool AudioPlayerController::Next(const bool play, std::string &diagnostic)
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->Step(1, play, diagnostic);
    }

    bool AudioPlayerController::Previous(const bool play, std::string &diagnostic)
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->voice.handle.IsValid() && impl_->voice.player->GetCurrentSecond() > 3.0f)
        {
            impl_->voice.player->SeekSeconds(0.0f);
            diagnostic.clear();
            return true;
        }
        return impl_->Step(-1, play, diagnostic);
    }

    bool AudioPlayerController::Seek(const float seconds)
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->voice.handle.IsValid() && impl_->voice.player->SeekSeconds(seconds);
    }

    bool AudioPlayerController::SetPlaybackRate(const float playback_rate)
    {
        if (!std::isfinite(playback_rate) || playback_rate < 0.5f || playback_rate > 2.0f)
        {
            return false;
        }
        std::lock_guard lock(impl_->mutex);
        impl_->playback_rate = playback_rate;
        if (!impl_->voice.handle.IsValid())
            return true;
        if (impl_->voice.buffer_player != nullptr)
            return impl_->voice.buffer_player->SetPlaybackRate(playback_rate);
        if (auto seekable = std::dynamic_pointer_cast<audio::SeekableAudioPlayer>(
                impl_->voice.player))
            return seekable->SetPlaybackRate(playback_rate);
        return false;
    }

    void AudioPlayerController::SetVolume(const float volume)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->volume = std::clamp(volume, 0.0f, 1.0f);
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.player->SetVolume(impl_->muted ? 0.0f : impl_->volume);
        }
    }

    void AudioPlayerController::SetMuted(const bool muted)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->muted = muted;
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.player->SetVolume(impl_->muted ? 0.0f : impl_->volume);
        }
    }

    void AudioPlayerController::SetLoopTrack(const bool enabled)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->loop_track = enabled;
        if (enabled)
        {
            impl_->shuffle = false;
        }
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.player->SetShouldLoop(enabled);
        }
    }

    void AudioPlayerController::SetShuffle(const bool enabled)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->shuffle = enabled;
        if (enabled)
        {
            impl_->loop_track = false;
            if (impl_->voice.handle.IsValid())
            {
                impl_->voice.player->SetShouldLoop(false);
            }
        }
    }

    void AudioPlayerController::Tick()
    {
        std::string diagnostic;
        bool advance = false;
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->voice.handle.IsValid() &&
                impl_->voice.player->GetCurrentState() == audio::AudioState::Finished &&
                !impl_->finished_handled)
            {
                impl_->finished_handled = true;
                impl_->last_status = "Finished";
                advance = impl_->shuffle;
            }
            else if (impl_->voice.handle.IsValid() &&
                     impl_->voice.player->GetCurrentState() != audio::AudioState::Finished)
            {
                impl_->finished_handled = false;
            }
        }
        if (advance)
        {
            Next(true, diagnostic);
        }
    }

    void AudioPlayerController::Shutdown() noexcept
    {
        if (impl_ != nullptr)
        {
            impl_->Shutdown();
        }
    }
}
