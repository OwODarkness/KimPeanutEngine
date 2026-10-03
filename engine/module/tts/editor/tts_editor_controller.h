#ifndef KPENGINE_MODULE_TTS_EDITOR_CONTROLLER_H
#define KPENGINE_MODULE_TTS_EDITOR_CONTROLLER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "module/tts/tts_system.h"
#include "runtime/audio/audio_system.h"
#include "tts_editor_settings.h"
#include "tts_editor_library.h"

namespace kpengine::tts_editor
{
    struct TtsEntryView
    {
        std::uint64_t id = 0;
        std::string text;
        std::string voice_name;
        tts::TTSJobState state = tts::TTSJobState::Queued;
        bool streaming = true;
        std::string error;
        tts::JobToken job{};
        audio::AudioHandle player{};
        bool durable = false;
        std::string artifact;
    };

    struct TtsEditorView
    {
        TtsEditorSettings settings;
        std::vector<TtsEntryView> entries;
        std::uint64_t selected_id = 0;
        audio::AudioState audio_state = audio::AudioState::Stopped;
        float elapsed_seconds = 0.0f;
        std::optional<float> duration_seconds;
        float volume = 0.8f;
        float playback_rate = 1.0f;
        bool settings_loaded = false;
        bool library_loaded = false;
        bool can_generate = false;
        std::string status;
        std::string error;
        std::string generation_blocker;
        std::string last_export_path;
    };

    class TtsEditorController final
    {
    public:
        using ProviderFactory = std::function<std::unique_ptr<tts::ITTSProvider>()>;
        explicit TtsEditorController(std::filesystem::path settings_path);
        TtsEditorController(std::filesystem::path settings_path,
                            std::unique_ptr<audio::AudioSystem> audio_system,
                            ProviderFactory provider_factory);
        ~TtsEditorController();

        bool Initialize(std::string &diagnostic);
        void Tick();
        void Shutdown() noexcept;
        TtsEditorView GetView() const;

        void QueueSettings(TtsEditorSettings settings);
        void QueueGenerate(std::string text);
        void QueueCancel();
        void QueueSelect(std::uint64_t id);
        void QueueTogglePlayPause();
        void QueueStop();
        void QueueSeek(float seconds);
        void QueuePlaybackRate(float rate);
        void QueueVolume(float volume);
        void QueueReloadSettings();
        void QueueImport(std::string path);
        void QueueDuplicate();
        void QueueDelete();
        void QueueExport(std::string basename);

    private:
        enum class ActionKind { SaveSettings, Generate, Cancel, Select,
                                TogglePlayPause, Stop, Seek, Rate, Volume, Reload,
                                Import, Duplicate, Delete, Export };
        struct Action
        {
            ActionKind kind;
            TtsEditorSettings settings;
            std::string text;
            std::uint64_t id = 0;
            float value = 0.0f;
        };
        void Queue(Action action);
        void ApplyAction(Action action);
        void Generate(std::string text);
        void CancelSelected();
        void CollectResults();
        void RefreshView();
        TtsEntryView *SelectedEntry();
        void RetirePlayer(TtsEntryView &entry);
        void EnsurePreview(TtsEntryView &entry);
        void PersistCompleted(TtsEntryView &entry);
        void Import(std::string path);
        void DuplicateSelected();
        void DeleteSelected();
        void ExportSelected(std::string basename);

        std::filesystem::path settings_path_;
        TtsEditorLibrary library_;
        std::vector<StoredDialog> durable_dialogs_;
        std::unordered_map<std::uint64_t,
            std::shared_ptr<const std::vector<std::uint8_t>>> pending_artifacts_;
        bool library_loaded_ = false;
        std::unique_ptr<audio::AudioSystem> audio_;
        tts::TTSSystem tts_;
        ProviderFactory provider_factory_;
        tts::ServerConfig active_server_{};
        bool provider_ready_ = false;
        std::vector<TtsEntryView> entries_;
        std::uint64_t selected_id_ = 0;
        std::uint64_t next_id_ = 1;
        float volume_ = 0.8f;
        mutable std::mutex view_mutex_;
        TtsEditorView view_;
        std::mutex actions_mutex_;
        std::vector<Action> actions_;
        std::mutex results_mutex_;
        std::vector<tts::TTSResult> results_;
    };
}

#endif
