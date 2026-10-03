#include "tts_editor_controller.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

#include "runtime/asset/miniaudio_audio_loader.h"
#include "tts_editor_wav.h"
#include "runtime/audio/audio_player.h"
#include "runtime/audio/buffer_audio_player.h"
#include "runtime/audio/miniaudio_audio_system.h"

namespace kpengine::tts_editor
{
    namespace
    {
        std::filesystem::path LibraryRootFromSettingsPath(const std::filesystem::path &path)
        {
            const auto parent = path.parent_path();
            const auto project = parent.filename() == "tts" ? parent.parent_path() : parent;
            return project / "save" / "tts_editor";
        }

        bool IsTerminal(const tts::TTSJobState state)
        {
            return state == tts::TTSJobState::Completed ||
                   state == tts::TTSJobState::Cancelled ||
                   state == tts::TTSJobState::Failed;
        }
    }

    TtsEditorController::TtsEditorController(std::filesystem::path settings_path)
        : TtsEditorController(std::move(settings_path),
                              std::make_unique<audio::MiniAudioSystem>(), {})
    {
    }

    TtsEditorController::TtsEditorController(
        std::filesystem::path settings_path,
        std::unique_ptr<audio::AudioSystem> audio_system,
        ProviderFactory provider_factory)
        : settings_path_(std::move(settings_path)),
          library_(LibraryRootFromSettingsPath(settings_path_)),
          audio_(std::move(audio_system)),
          provider_factory_(std::move(provider_factory))
    {
        tts_.audio_system = audio_.get();
    }

    TtsEditorController::~TtsEditorController()
    {
        Shutdown();
    }

    bool TtsEditorController::Initialize(std::string &diagnostic)
    {
        TtsEditorSettings settings;
        const bool loaded = LoadOrCreateSettings(settings_path_, settings, diagnostic);
        std::string library_diagnostic;
        library_loaded_ = library_.Load(durable_dialogs_, next_id_, library_diagnostic);
        if (library_loaded_)
        {
            for (const StoredDialog &stored : durable_dialogs_)
            {
                TtsEntryView entry;
                entry.id = stored.id;
                entry.text = stored.text;
                entry.voice_name = stored.voice_name;
                entry.state = tts::TTSJobState::Completed;
                entry.streaming = false;
                entry.durable = true;
                entry.artifact = stored.artifact;
                entries_.push_back(std::move(entry));
            }
            if (!entries_.empty()) selected_id_ = entries_.back().id;
        }
        std::lock_guard lock(view_mutex_);
        view_.settings = std::move(settings);
        view_.settings_loaded = loaded;
        view_.error = loaded ? std::string{} : diagnostic;
        view_.status = loaded ? "Settings loaded" : "Settings need repair";
        view_.library_loaded = library_loaded_;
        if (!library_loaded_)
        {
            view_.error = library_diagnostic;
            view_.status = "TTS library needs repair";
        }
        ValidateForGeneration(view_.settings, view_.generation_blocker);
        view_.can_generate = loaded && view_.generation_blocker.empty();
        return true;
    }

    void TtsEditorController::Queue(Action action)
    {
        std::lock_guard lock(actions_mutex_);
        actions_.push_back(std::move(action));
    }

    void TtsEditorController::QueueSettings(TtsEditorSettings settings)
    {
        Action action{ActionKind::SaveSettings};
        action.settings = std::move(settings);
        Queue(std::move(action));
    }

    void TtsEditorController::QueueGenerate(std::string text)
    {
        Action action{ActionKind::Generate};
        action.text = std::move(text);
        Queue(std::move(action));
    }

    void TtsEditorController::QueueCancel() { Queue({ActionKind::Cancel}); }
    void TtsEditorController::QueueSelect(std::uint64_t id)
    {
        Action action{ActionKind::Select};
        action.id = id;
        Queue(std::move(action));
    }
    void TtsEditorController::QueueTogglePlayPause() { Queue({ActionKind::TogglePlayPause}); }
    void TtsEditorController::QueueStop() { Queue({ActionKind::Stop}); }
    void TtsEditorController::QueueSeek(float seconds)
    {
        Action action{ActionKind::Seek};
        action.value = seconds;
        Queue(std::move(action));
    }
    void TtsEditorController::QueuePlaybackRate(float rate)
    {
        Action action{ActionKind::Rate};
        action.value = rate;
        Queue(std::move(action));
    }
    void TtsEditorController::QueueVolume(float volume)
    {
        Action action{ActionKind::Volume};
        action.value = volume;
        Queue(std::move(action));
    }
    void TtsEditorController::QueueReloadSettings() { Queue({ActionKind::Reload}); }
    void TtsEditorController::QueueImport(std::string path)
    {
        Action action{ActionKind::Import}; action.text = std::move(path); Queue(std::move(action));
    }
    void TtsEditorController::QueueDuplicate() { Queue({ActionKind::Duplicate}); }
    void TtsEditorController::QueueDelete() { Queue({ActionKind::Delete}); }
    void TtsEditorController::QueueExport(std::string basename)
    {
        Action action{ActionKind::Export}; action.text = std::move(basename); Queue(std::move(action));
    }

    TtsEntryView *TtsEditorController::SelectedEntry()
    {
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [this](const TtsEntryView &entry) { return entry.id == selected_id_; });
        return found == entries_.end() ? nullptr : &*found;
    }

    void TtsEditorController::RetirePlayer(TtsEntryView &entry)
    {
        if (entry.player.IsValid())
        {
            audio_->DestroyAudioPlayer(entry.player);
            entry.player = {};
        }
    }

    void TtsEditorController::CancelSelected()
    {
        TtsEntryView *entry = SelectedEntry();
        if (!entry || IsTerminal(entry->state))
            return;
        if (!entry->job.IsValid() || !tts_.Cancel(entry->job))
            return;
        RetirePlayer(*entry);
        pending_artifacts_.erase(entry->id);
        entry->state = tts::TTSJobState::Cancelled;
    }

    void TtsEditorController::Generate(std::string text)
    {
        TtsEditorSettings settings;
        bool loaded = false;
        {
            std::lock_guard lock(view_mutex_);
            settings = view_.settings;
            loaded = view_.settings_loaded;
        }
        std::string diagnostic;
        if (!loaded || !ValidateForGeneration(settings, diagnostic))
        {
            std::lock_guard lock(view_mutex_);
            view_.error = loaded ? diagnostic : "Repair TTS settings before generating";
            return;
        }
        const TtsVoicePreset *voice = SelectedVoice(settings);
        if (text.empty() || text.size() > 4096)
        {
            std::lock_guard lock(view_mutex_);
            view_.error = "Enter 1 to 4096 bytes of text";
            return;
        }
        if (!audio_->IsInitialized() && !audio_->Initialize())
        {
            std::lock_guard lock(view_mutex_);
            view_.error = "Could not initialize the audio output device";
            return;
        }
        if (!provider_ready_)
        {
            active_server_ = {settings.address, settings.port, settings.api_path,
                              settings.timeout_seconds};
            provider_ready_ = provider_factory_
                ? tts_.InitializeWithProvider(provider_factory_(), active_server_)
                : tts_.Initialize(tts::TTSProviderType::GPT_SOVITS, active_server_);
            if (!provider_ready_)
            {
                std::lock_guard lock(view_mutex_);
                view_.error = "Could not initialize the TTS provider";
                return;
            }
        }
        for (auto &prior : entries_)
        {
            if (!IsTerminal(prior.state))
            {
                if (prior.job.IsValid())
                    tts_.Cancel(prior.job);
                prior.state = tts::TTSJobState::Cancelled;
                RetirePlayer(prior);
            }
            if (const auto player = audio_->GetAudioPlayer(prior.player))
                player->Stop();
        }
        constexpr std::size_t kMaximumEntries = 32;
        if (entries_.size() == kMaximumEntries)
        {
            RetirePlayer(entries_.front());
            entries_.erase(entries_.begin());
        }
        TtsEntryView entry;
        entry.id = next_id_++;
        entry.text = std::move(text);
        entry.voice_name = voice->name;
        entry.streaming = settings.streaming;
        selected_id_ = entry.id;
        tts::TTSRequest request;
        request.turn_id = entry.id;
        request.text = entry.text;
        request.text_lang = settings.text_language;
        request.ref_audio_path = voice->ref_audio_path;
        request.prompt_text = voice->ref_text;
        request.prompt_lang = voice->ref_language;
        request.streaming = settings.streaming;
        entries_.push_back(std::move(entry));
        const auto token = tts_.AsyncSynthesize(request, [this](const tts::TTSResult &result) {
            std::lock_guard lock(results_mutex_);
            results_.push_back(result);
        });
        entries_.back().job = token;
        if (!token.IsValid())
        {
            entries_.back().state = tts::TTSJobState::Failed;
            entries_.back().error = "TTS queue is unavailable or full";
        }
        std::lock_guard lock(view_mutex_);
        view_.error.clear();
        view_.status = token.IsValid() ? "Speech generation queued" : "Speech generation rejected";
    }

    void TtsEditorController::ApplyAction(Action action)
    {
        switch (action.kind)
        {
        case ActionKind::SaveSettings:
        {
            TtsEditorSettings previous;
            bool loaded = false;
            {
                std::lock_guard lock(view_mutex_);
                previous = view_.settings;
                loaded = view_.settings_loaded;
            }
            if (!loaded)
            {
                std::lock_guard lock(view_mutex_);
                view_.error = "Repair the invalid settings file and reload it before saving";
                break;
            }
            std::string diagnostic;
            if (!SaveSettings(settings_path_, action.settings, diagnostic))
            {
                std::lock_guard lock(view_mutex_);
                view_.error = std::move(diagnostic);
                break;
            }
            if (previous.address != action.settings.address ||
                previous.port != action.settings.port ||
                previous.api_path != action.settings.api_path ||
                previous.timeout_seconds != action.settings.timeout_seconds)
            {
                for (auto &entry : entries_)
                {
                    if (!IsTerminal(entry.state) && entry.job.IsValid())
                    {
                        tts_.Cancel(entry.job);
                        entry.state = tts::TTSJobState::Cancelled;
                    }
                    RetirePlayer(entry);
                }
                tts_.ShutDown();
                provider_ready_ = false;
            }
            std::lock_guard lock(view_mutex_);
            view_.settings = std::move(action.settings);
            view_.status = "TTS settings saved";
            view_.error.clear();
            break;
        }
        case ActionKind::Generate: Generate(std::move(action.text)); break;
        case ActionKind::Cancel: CancelSelected(); break;
        case ActionKind::Select:
            selected_id_ = action.id;
            if (auto *entry = SelectedEntry(); entry && entry->durable) EnsurePreview(*entry);
            break;
        case ActionKind::Reload:
        {
            TtsEditorSettings settings;
            std::string diagnostic;
            const bool loaded = LoadOrCreateSettings(settings_path_, settings, diagnostic);
            if (loaded && provider_ready_ &&
                (active_server_.host != settings.address ||
                 active_server_.port != settings.port ||
                 active_server_.api_path != settings.api_path ||
                 active_server_.timeout != settings.timeout_seconds))
            {
                for (auto &entry : entries_)
                {
                    if (!IsTerminal(entry.state) && entry.job.IsValid())
                    {
                        tts_.Cancel(entry.job);
                        entry.state = tts::TTSJobState::Cancelled;
                    }
                    RetirePlayer(entry);
                }
                tts_.ShutDown();
                provider_ready_ = false;
            }
            std::lock_guard lock(view_mutex_);
            view_.settings_loaded = loaded;
            if (loaded)
            {
                view_.settings = std::move(settings);
                view_.error.clear();
                view_.status = "Settings reloaded";
            }
            else view_.error = std::move(diagnostic);
            break;
        }
        case ActionKind::Volume:
            volume_ = std::clamp(action.value, 0.0f, 1.0f);
            if (auto *entry = SelectedEntry())
                if (const auto player = audio_->GetAudioPlayer(entry->player))
                    player->SetVolume(volume_);
            break;
        case ActionKind::TogglePlayPause:
        case ActionKind::Stop:
        case ActionKind::Seek:
        case ActionKind::Rate:
        {
            auto *entry = SelectedEntry();
            if (!entry) break;
            if (entry->durable && !entry->player.IsValid()) EnsurePreview(*entry);
            const auto player = audio_->GetAudioPlayer(entry->player);
            if (!player) break;
            if (action.kind == ActionKind::TogglePlayPause)
            {
                const audio::AudioState state = player->GetCurrentState();
                if (state == audio::AudioState::Playing ||
                    state == audio::AudioState::Buffering)
                    player->Pause();
                else if (state == audio::AudioState::Finished && !entry->streaming)
                    player->Restart();
                else if (state == audio::AudioState::Finished && entry->streaming)
                    break;
                else player->Play();
            }
            else if (action.kind == ActionKind::Stop)
            {
                if (IsTerminal(entry->state)) player->Stop();
            }
            else if (!entry->streaming)
            {
                if (action.kind == ActionKind::Seek)
                    player->SeekSeconds(action.value);
                else if (const auto buffer =
                    std::dynamic_pointer_cast<audio::BufferAudioPlayer>(player))
                    buffer->SetPlaybackRate(action.value);
            }
            break;
        }
        case ActionKind::Import: Import(std::move(action.text)); break;
        case ActionKind::Duplicate: DuplicateSelected(); break;
        case ActionKind::Delete: DeleteSelected(); break;
        case ActionKind::Export: ExportSelected(std::move(action.text)); break;
        }
    }

    void TtsEditorController::EnsurePreview(TtsEntryView &entry)
    {
        if (entry.player.IsValid() || entry.artifact.empty()) return;
        if (!audio_->IsInitialized() && !audio_->Initialize())
        {
            entry.error = "Could not initialize the audio output device";
            return;
        }
        std::vector<std::uint8_t> wav;
        std::string diagnostic;
        if (!library_.Read(entry.artifact, wav, diagnostic))
        {
            entry.error = std::move(diagnostic);
            return;
        }
        asset::MiniAudio_AudioLoader loader;
        const auto decoded = loader.LoadFromMemory(
            reinterpret_cast<const char *>(wav.data()), wav.size());
        if (!decoded.data || decoded.data->frame_count == 0)
        {
            entry.error = "Could not decode the stored WAV for preview";
            return;
        }
        const auto handle = audio_->CreateAudioPlayer(audio::AudioPlayerType::Buffer);
        const auto player = std::dynamic_pointer_cast<audio::BufferAudioPlayer>(
            audio_->GetAudioPlayer(handle));
        if (!handle.IsValid() || !player)
        {
            if (handle.IsValid()) audio_->DestroyAudioPlayer(handle);
            entry.error = "Could not create a buffer preview player";
            return;
        }
        player->SetClip(decoded.data);
        player->SetVolume(volume_);
        entry.player = handle;
        entry.streaming = false;
        entry.error.clear();
    }

    void TtsEditorController::PersistCompleted(TtsEntryView &entry)
    {
        if (entry.durable) return;
        if (!library_loaded_)
        {
            entry.error = "Repair the TTS library manifest before saving completed speech";
            return;
        }
        const auto artifact = pending_artifacts_.find(entry.id);
        if (artifact == pending_artifacts_.end() || !artifact->second) return;
        std::string diagnostic;
        std::string name = entry.artifact;
        if (name.empty() && !library_.Store(entry.id, *artifact->second, name, diagnostic))
        {
            entry.error = std::move(diagnostic);
            return;
        }
        entry.artifact = name;
        StoredDialog stored{entry.id, entry.text, entry.voice_name,
                            entry.streaming, std::move(name)};
        durable_dialogs_.push_back(stored);
        if (!library_.Save(durable_dialogs_, next_id_, diagnostic))
        {
            durable_dialogs_.pop_back();
            entry.error = std::move(diagnostic);
            return;
        }
        entry.artifact = stored.artifact;
        entry.durable = true;
        entry.error.clear();
        pending_artifacts_.erase(artifact);
        std::lock_guard lock(view_mutex_);
        view_.library_loaded = true;
        view_.error.clear();
        view_.status = "Speech saved to dialog library";
    }

    void TtsEditorController::Import(std::string path)
    {
        if (!library_loaded_) { std::lock_guard lock(view_mutex_); view_.error = "Repair the TTS library manifest before importing"; return; }
        if (path.empty()) { std::lock_guard lock(view_mutex_); view_.error = "Choose a WAV file to import"; return; }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > kMaximumWavBytes)
        { std::lock_guard lock(view_mutex_); view_.error = "Import WAV is missing, empty, or exceeds 32 MiB"; return; }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        if (!input || !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        { std::lock_guard lock(view_mutex_); view_.error = "Could not read import WAV"; return; }
        if (next_id_ == 0 || next_id_ == std::numeric_limits<std::uint64_t>::max())
        { std::lock_guard lock(view_mutex_); view_.error = "Dialog ID capacity exhausted"; return; }
        std::string artifact, diagnostic;
        if (!library_.Store(next_id_, bytes, artifact, diagnostic))
        { std::lock_guard lock(view_mutex_); view_.error = std::move(diagnostic); return; }
        StoredDialog stored{next_id_++, std::filesystem::path(path).stem().string(), "Imported", false, artifact};
        durable_dialogs_.push_back(stored);
        if (!library_.Save(durable_dialogs_, next_id_, diagnostic))
        {
            durable_dialogs_.pop_back(); --next_id_;
            std::lock_guard lock(view_mutex_); view_.error = std::move(diagnostic); return;
        }
        TtsEntryView entry;
        entry.id = stored.id; entry.text = stored.text; entry.voice_name = stored.voice_name;
        entry.state = tts::TTSJobState::Completed; entry.durable = true; entry.artifact = artifact;
        entries_.push_back(std::move(entry)); selected_id_ = stored.id;
        std::lock_guard lock(view_mutex_); view_.error.clear(); view_.status = "WAV imported"; view_.library_loaded = true;
    }

    void TtsEditorController::DuplicateSelected()
    {
        if (!library_loaded_) { std::lock_guard lock(view_mutex_); view_.error = "Repair the TTS library manifest before editing dialogs"; return; }
        auto *entry = SelectedEntry();
        if (!entry || !entry->durable) { std::lock_guard lock(view_mutex_); view_.error = "Select a saved dialog to duplicate"; return; }
        if (next_id_ == 0 || next_id_ == std::numeric_limits<std::uint64_t>::max()) return;
        StoredDialog duplicate{next_id_++, entry->text, entry->voice_name, entry->streaming, entry->artifact};
        durable_dialogs_.push_back(duplicate);
        std::string diagnostic;
        if (!library_.Save(durable_dialogs_, next_id_, diagnostic))
        { durable_dialogs_.pop_back(); --next_id_; std::lock_guard lock(view_mutex_); view_.error = std::move(diagnostic); return; }
        TtsEntryView copy = *entry; copy.id = duplicate.id; copy.job = {}; copy.player = {}; copy.error.clear();
        entries_.push_back(std::move(copy)); selected_id_ = duplicate.id;
        std::lock_guard lock(view_mutex_); view_.error.clear(); view_.status = "Dialog duplicated";
    }

    void TtsEditorController::DeleteSelected()
    {
        if (!library_loaded_) { std::lock_guard lock(view_mutex_); view_.error = "Repair the TTS library manifest before editing dialogs"; return; }
        auto *entry = SelectedEntry();
        if (!entry) return;
        if (!entry->durable) { std::lock_guard lock(view_mutex_); view_.error = "Only saved dialogs can be deleted"; return; }
        const auto old = durable_dialogs_;
        std::erase_if(durable_dialogs_, [this](const StoredDialog &row) { return row.id == selected_id_; });
        std::string diagnostic;
        if (!library_.Save(durable_dialogs_, next_id_, diagnostic))
        { durable_dialogs_ = old; std::lock_guard lock(view_mutex_); view_.error = std::move(diagnostic); return; }
        const auto deleted_id = selected_id_;
        std::erase_if(entries_, [deleted_id](const TtsEntryView &row) { return row.id == deleted_id; });
        selected_id_ = entries_.empty() ? 0 : entries_.back().id;
        std::lock_guard lock(view_mutex_); view_.error.clear(); view_.status = "Dialog deleted";
    }

    void TtsEditorController::ExportSelected(std::string basename)
    {
        auto *entry = SelectedEntry();
        if (!entry || !entry->durable) { std::lock_guard lock(view_mutex_); view_.error = "Select a saved dialog to export"; return; }
        TtsEditorSettings settings;
        { std::lock_guard lock(view_mutex_); settings = view_.settings; }
        std::filesystem::path path;
        std::string diagnostic;
        if (!library_.Export(entry->artifact, settings.output_directory, basename, path, diagnostic))
        { std::lock_guard lock(view_mutex_); view_.error = std::move(diagnostic); return; }
        std::lock_guard lock(view_mutex_); view_.last_export_path = path.string(); view_.error.clear(); view_.status = "WAV exported";
    }

    void TtsEditorController::CollectResults()
    {
        std::vector<tts::TTSResult> results;
        {
            std::lock_guard lock(results_mutex_);
            results.swap(results_);
        }
        for (const auto &result : results)
        {
            const auto found = std::find_if(entries_.begin(), entries_.end(),
                [&result](const TtsEntryView &entry) { return entry.id == result.turn_id; });
            if (found == entries_.end() || found->state == tts::TTSJobState::Cancelled ||
                found->job != result.job)
            {
                if (result.player_handle.IsValid())
                    audio_->DestroyAudioPlayer(result.player_handle);
                continue;
            }
            if (result.success && result.player_handle.IsValid())
            {
                found->player = result.player_handle;
                if (result.wav_bytes && !result.wav_bytes->empty())
                    pending_artifacts_[found->id] = result.wav_bytes;
                if (const auto player = audio_->GetAudioPlayer(found->player))
                    player->SetVolume(volume_);
            }
            else
            {
                found->state = tts::TTSJobState::Failed;
                found->error = result.error_message;
            }
        }
        for (const auto &event : tts_.DrainEvents())
        {
            if (event.type != tts::TTSJobEventType::StateChanged)
                continue;
            const auto found = std::find_if(entries_.begin(), entries_.end(),
                [&event](const TtsEntryView &entry) { return entry.id == event.turn_id; });
            if (found == entries_.end() || found->state == tts::TTSJobState::Cancelled ||
                found->job != event.job)
                continue;
            found->state = event.state;
            if (event.state == tts::TTSJobState::Failed)
                found->error = event.message;
            if (event.state == tts::TTSJobState::Failed ||
                event.state == tts::TTSJobState::Cancelled)
            {
                RetirePlayer(*found);
                pending_artifacts_.erase(found->id);
            }
            if (event.state == tts::TTSJobState::Completed)
                PersistCompleted(*found);
        }
        for (auto &entry : entries_)
            if (entry.state == tts::TTSJobState::Completed && !entry.durable)
                PersistCompleted(entry);
    }

    void TtsEditorController::RefreshView()
    {
        std::lock_guard lock(view_mutex_);
        view_.entries = entries_;
        view_.selected_id = selected_id_;
        view_.volume = volume_;
        view_.audio_state = audio::AudioState::Stopped;
        view_.elapsed_seconds = 0.0f;
        view_.duration_seconds.reset();
        view_.playback_rate = 1.0f;
        if (const auto *entry = SelectedEntry())
        {
            if (const auto player = audio_->GetAudioPlayer(entry->player))
            {
                view_.audio_state = player->GetCurrentState();
                view_.elapsed_seconds = player->GetCurrentSecond();
                view_.playback_rate = player->GetPlaybackRate();
                if (!entry->streaming)
                    view_.duration_seconds = player->GetCurrentSecond() +
                                             player->GetRemainSecond();
            }
        }
        ValidateForGeneration(view_.settings, view_.generation_blocker);
        view_.can_generate = view_.settings_loaded && view_.generation_blocker.empty();
    }

    void TtsEditorController::Tick()
    {
        std::vector<Action> actions;
        {
            std::lock_guard lock(actions_mutex_);
            actions.swap(actions_);
        }
        for (auto &action : actions)
            ApplyAction(std::move(action));
        CollectResults();
        RefreshView();
    }

    TtsEditorView TtsEditorController::GetView() const
    {
        std::lock_guard lock(view_mutex_);
        return view_;
    }

    void TtsEditorController::Shutdown() noexcept
    {
        tts_.ShutDown();
        provider_ready_ = false;
        for (auto &entry : entries_)
            RetirePlayer(entry);
        entries_.clear();
        pending_artifacts_.clear();
        audio_->ShutDown();
    }
}
