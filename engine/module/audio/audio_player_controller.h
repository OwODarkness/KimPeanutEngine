#ifndef KPENGINE_MODULE_AUDIO_AUDIO_PLAYER_CONTROLLER_H
#define KPENGINE_MODULE_AUDIO_AUDIO_PLAYER_CONTROLLER_H

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
        std::uint64_t file_size = 0;
        float duration_seconds = 0.0f;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        bool favorite = false;
        std::array<float, 384> waveform{};
    };

    struct PlaybackView
    {
        std::optional<TrackView> track;
        audio::AudioState state = audio::AudioState::Stopped;
        float position_seconds = 0.0f;
        float duration_seconds = 0.0f;
        float volume = 0.8f;
        bool muted = false;
        bool loop_track = false;
        bool shuffle = false;
        bool can_seek = false;
        std::string status;
        std::string error;
        std::size_t pending_imports = 0;
        std::size_t queue_size = 0;
        float rms = 0.0f;
        float peak = 0.0f;
        std::array<float, 48> spectrum{};
    };

    class AudioPlayerController final
    {
    public:
        explicit AudioPlayerController(audio::MiniAudioSystem &audio_system);
        ~AudioPlayerController();

        AudioPlayerController(const AudioPlayerController &) = delete;
        AudioPlayerController &operator=(const AudioPlayerController &) = delete;

        bool ImportFile(std::string path, std::string &diagnostic);
        bool ImportFolder(std::string path, std::string &diagnostic);
        std::vector<TrackView> GetQueue() const;
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
