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
#include <thread>
#include <unordered_set>
#include <string_view>

#include "runtime/asset/asset_manager.h"
#include "runtime/asset/audio.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/audio/miniaudio_audio_system.h"
#include "log/logger.h"

namespace kpengine::audio_player
{
    namespace
    {
        constexpr std::uint64_t kMaximumFileBytes = 256ull * 1024ull * 1024ull;
        constexpr std::size_t kMaximumQueueTracks = 512;
        constexpr std::size_t kMaximumFolderFiles = 512;
        constexpr std::array<std::string_view, 4> kSupportedExtensions{
            ".wav", ".mp3", ".ogg", ".flac"};

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

        std::array<float, 384> BuildWaveform(const data::AudioClip &clip)
        {
            std::array<float, 384> peaks{};
            const std::uint64_t channels = clip.format.channels;
            if (channels == 0 || clip.frame_count == 0 ||
                clip.pcm.size() < clip.frame_count * channels)
            {
                return peaks;
            }

            for (std::size_t bin = 0; bin < peaks.size(); ++bin)
            {
                const std::uint64_t first = clip.frame_count * bin / peaks.size();
                const std::uint64_t last = std::max(first + 1,
                    clip.frame_count * (bin + 1) / peaks.size());
                float peak = 0.0f;
                for (std::uint64_t frame = first; frame < std::min(last, clip.frame_count); ++frame)
                {
                    for (std::uint64_t channel = 0; channel < channels; ++channel)
                    {
                        const float sample = clip.pcm[static_cast<std::size_t>(frame * channels + channel)];
                        peak = std::max(peak, std::abs(sample));
                    }
                }
                peaks[bin] = std::clamp(peak, 0.0f, 1.0f);
            }
            return peaks;
        }

        struct SignalSnapshot
        {
            float rms = 0.0f;
            float peak = 0.0f;
            std::array<float, 48> spectrum{};
        };

        SignalSnapshot SampleSignal(const data::AudioClip &clip, const std::uint64_t played_frame)
        {
            SignalSnapshot signal{};
            constexpr std::size_t count = 256;
            constexpr float pi = 3.14159265358979323846f;
            std::array<std::complex<float>, count> bins{};
            const std::size_t channels = clip.format.channels;
            if (channels == 0 || clip.frame_count == 0 ||
                clip.frame_count > clip.pcm.size() / channels)
                return signal;

            float energy = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint64_t source_frame = played_frame >= count - i
                    ? played_frame - (count - i) : clip.frame_count;
                if (source_frame >= clip.frame_count)
                    continue;
                float sample = 0.0f;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    sample += clip.pcm[static_cast<std::size_t>(source_frame) * channels + channel];
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
        };

        struct ImportJob
        {
            std::filesystem::path path;
            std::uint64_t file_size = 0;
        };

        struct Voice
        {
            audio::AudioHandle handle{};
            std::shared_ptr<audio::AudioPlayer> player;
            std::shared_ptr<audio::BufferAudioPlayer> buffer_player;
            std::shared_ptr<const data::AudioClip> clip;
            std::uint64_t track_id = 0;
        };

        SignalSnapshot CurrentSignal()
        {
            if (!voice.player || !voice.clip || !audio_system->IsInitialized() ||
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
                signal_cache = SampleSignal(*voice.clip, frame);
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

        bool EnqueueImport(const std::filesystem::path &path, std::string &diagnostic)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                diagnostic = "File does not exist or is not a regular file";
                SetImportError(diagnostic);
                return false;
            }
            if (!IsSupported(path))
            {
                diagnostic = "Supported formats are WAV, MP3, OGG, and FLAC";
                SetImportError(diagnostic);
                return false;
            }
            const std::uint64_t size = std::filesystem::file_size(path, error);
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

            const std::string key = CanonicalKey(path);
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
            if (queued_paths.contains(key) || track_paths.contains(key))
            {
                diagnostic = "This file is already in the queue or importing";
                last_error = diagnostic;
                return false;
            }

            import_jobs.push_back({path, size});
            queued_paths.insert(key);
            last_status = "Import queued: " + PathUtf8(path.filename());
            last_error.clear();
            import_changed.notify_one();
            diagnostic.clear();
            return true;
        }

        void RunImports()
        {
            for (;;)
            {
                ImportJob job;
                {
                    std::unique_lock lock(mutex);
                    import_changed.wait(lock, [this] { return stopping || !import_jobs.empty(); });
                    if (stopping && import_jobs.empty())
                    {
                        return;
                    }
                    job = std::move(import_jobs.front());
                    import_jobs.pop_front();
                    import_active = true;
                    last_status = "Decoding: " + PathUtf8(job.path.filename());
                }

                std::string failure;
                std::string path = PathUtf8(job.path);
                std::shared_ptr<asset::AudioResource> resource;
                try
                {
                    const asset::AssetID asset_id =
                        asset::AssetManager::GetInstance().LoadSync(path);
                    resource = asset_id.IsValid()
                        ? asset::AssetManager::GetInstance().GetResource<asset::AudioResource>(asset_id)
                        : nullptr;
                }
                catch (const std::exception &exception)
                {
                    failure = exception.what();
                }
                if (resource == nullptr || resource->data == nullptr ||
                    resource->data->frame_count == 0 ||
                    resource->data->format.channels == 0 ||
                    resource->data->format.sample_rate == 0)
                {
                    failure = "Could not decode this file as playable audio";
                }

                std::lock_guard lock(mutex);
                import_active = false;
                const std::string key = CanonicalKey(job.path);
                queued_paths.erase(key);
                if (failure.empty() && tracks.size() < kMaximumQueueTracks)
                {
                    Track track{};
                    track.view.id = next_track_id++;
                    track.view.path = path;
                    track.view.name = PathUtf8(job.path.stem());
                    track.view.extension = Extension(job.path);
                    track.view.file_size = job.file_size;
                    track.view.duration_seconds = resource->data->GetDuration();
                    track.view.sample_rate = resource->data->format.sample_rate;
                    track.view.channels = resource->data->format.channels;
                    track.view.waveform = BuildWaveform(*resource->data);
                    track.clip = std::move(resource->data);
                    track_paths.insert(key);
                    if (!selected_track_id.has_value())
                    {
                        selected_track_id = track.view.id;
                    }
                    last_status = "Imported: " + track.view.name;
                    tracks.push_back(std::move(track));
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
                    last_status = "Import failed: " + PathUtf8(job.path.filename());
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
                diagnostic.clear();
                return true;
            }
            DestroyVoice();
            const audio::AudioHandle handle =
                audio_system->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
            auto player = audio_system->GetAudioPlayer(handle);
            auto buffer = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(player);
            if (!handle.IsValid() || buffer == nullptr)
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
            buffer->SetClip(track.clip);
            buffer->SetBus(audio::AudioBus::Music);
            buffer->SetVolume(muted ? 0.0f : volume);
            buffer->SetShouldLoop(loop_track);
            voice = {handle, std::move(player), std::move(buffer), track.clip, track.view.id};
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
                voice.buffer_player->Restart();
            }
            else
            {
                voice.buffer_player->Play();
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
            import_jobs.clear();
            queued_paths.clear();
            track_paths.clear();
        }

        audio::MiniAudioSystem *audio_system;
        mutable std::mutex mutex;
        std::condition_variable import_changed;
        std::deque<ImportJob> import_jobs;
        std::vector<Track> tracks;
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
        bool muted = false;
        bool loop_track = false;
        bool shuffle = false;
        bool import_active = false;
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
        return impl_->EnqueueImport(FromUtf8(path), diagnostic);
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
                               : "No WAV, MP3, OGG, or FLAC files were found";
            return false;
        }

        std::size_t accepted = 0;
        std::string last_failure;
        for (const auto &file : files)
        {
            std::string file_diagnostic;
            if (impl_->EnqueueImport(file, file_diagnostic))
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

    PlaybackView AudioPlayerController::GetPlaybackView() const
    {
        std::lock_guard lock(impl_->mutex);
        PlaybackView result{};
        if (impl_->selected_track_id.has_value())
        {
            if (const auto *track = impl_->FindTrack(*impl_->selected_track_id))
            {
                result.track = track->view;
                result.duration_seconds = track->view.duration_seconds;
            }
        }
        if (impl_->voice.handle.IsValid() && impl_->voice.player != nullptr)
        {
            result.state = impl_->voice.player->GetCurrentState();
            result.position_seconds = impl_->voice.buffer_player->GetCurrentSecond();
            result.volume = impl_->volume;
            result.muted = impl_->muted;
            result.can_seek = true;
        }
        else
        {
            result.volume = impl_->volume;
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
                impl_->voice.buffer_player->Play();
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
                    impl_->voice.buffer_player->Pause();
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
            impl_->voice.buffer_player->Pause();
            impl_->last_status = "Paused";
        }
    }

    void AudioPlayerController::Stop()
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.buffer_player->Stop();
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
        if (impl_->voice.handle.IsValid() && impl_->voice.buffer_player->GetCurrentSecond() > 3.0f)
        {
            impl_->voice.buffer_player->SeekSeconds(0.0f);
            diagnostic.clear();
            return true;
        }
        return impl_->Step(-1, play, diagnostic);
    }

    bool AudioPlayerController::Seek(const float seconds)
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->voice.handle.IsValid() && impl_->voice.buffer_player->SeekSeconds(seconds);
    }

    void AudioPlayerController::SetVolume(const float volume)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->volume = std::clamp(volume, 0.0f, 1.0f);
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.buffer_player->SetVolume(impl_->muted ? 0.0f : impl_->volume);
        }
    }

    void AudioPlayerController::SetMuted(const bool muted)
    {
        std::lock_guard lock(impl_->mutex);
        impl_->muted = muted;
        if (impl_->voice.handle.IsValid())
        {
            impl_->voice.buffer_player->SetVolume(impl_->muted ? 0.0f : impl_->volume);
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
            impl_->voice.buffer_player->SetShouldLoop(enabled);
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
                impl_->voice.buffer_player->SetShouldLoop(false);
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
