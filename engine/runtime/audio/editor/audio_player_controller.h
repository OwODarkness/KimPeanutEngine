#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_CONTROLLER_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_CONTROLLER_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "runtime/audio/audio_types.h"

namespace kpengine::audio
{
    class MiniAudioSystem;
}

namespace kpengine::audio_player
{
    struct TrackView
    {
        std::uint64_t id = 0;
        std::string path;
        std::string name;
        std::string extension;
        std::string content_id;
        std::string product_path;
        std::uint64_t file_size = 0;
        float duration_seconds = 0.0f;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        bool favorite = false;
        bool native_product = false;
        bool project_asset = false;
        bool has_subtitles = false;
        std::uint32_t subtitle_cue_count = 0;
        std::string subtitle_language;
        std::shared_ptr<const std::vector<float>> waveform;
    };

    struct PlaybackView
    {
        std::optional<TrackView> track;
        audio::AudioState state = audio::AudioState::Stopped;
        float position_seconds = 0.0f;
        float duration_seconds = 0.0f;
        float volume = 0.8f;
        float playback_rate = 1.0f;
        bool muted = false;
        bool loop_track = false;
        bool shuffle = false;
        bool can_seek = false;
        std::string status;
        std::string error;
        std::string subtitle_text;
        std::string subtitle_language;
        bool subtitle_track_attached = false;
        std::size_t pending_imports = 0;
        std::size_t queue_size = 0;
        float rms = 0.0f;
        float peak = 0.0f;
        std::array<float, 48> spectrum{};
    };

    struct ProjectLibraryView
    {
        std::vector<TrackView> tracks;
        std::string status{"Not refreshed"};
        std::string error;
        bool refreshing{false};
    };

    class AudioPlayerController final
    {
    public:
        explicit AudioPlayerController(audio::MiniAudioSystem &audio_system);
        ~AudioPlayerController();

        AudioPlayerController(const AudioPlayerController &) = delete;
        AudioPlayerController &operator=(const AudioPlayerController &) = delete;

        bool ImportFile(std::string path, std::string &diagnostic);
        bool ImportFile(std::string path, std::string subtitle_path,
                        std::string &diagnostic);
        bool ReimportSelected(std::string subtitle_path, std::string &diagnostic);
        bool ImportFolder(std::string path, std::string &diagnostic);
        std::vector<TrackView> GetQueue() const;
        ProjectLibraryView GetProjectLibrary() const;
        bool RefreshProjectLibrary(std::string &diagnostic);
        bool QueueProjectTrack(std::string content_id, std::string &diagnostic);
        PlaybackView GetPlaybackView() const;

        bool Select(std::uint64_t track_id);
        bool ToggleFavorite(std::uint64_t track_id);
        bool RemoveSelected();
        void ClearQueue();
        bool PlaySelected(std::string &diagnostic);
        bool TogglePlayPause(std::string &diagnostic);
        void Pause();
        void Stop();
        bool Next(bool play, std::string &diagnostic);
        bool Previous(bool play, std::string &diagnostic);
        bool Seek(float seconds);
        bool SetPlaybackRate(float playback_rate);
        void SetVolume(float volume);
        void SetMuted(bool muted);
        void SetLoopTrack(bool enabled);
        void SetShuffle(bool enabled);
        void Tick();
        void Shutdown() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
