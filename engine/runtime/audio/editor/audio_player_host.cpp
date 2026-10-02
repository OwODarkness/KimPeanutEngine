#include "audio_player_host.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "audio_player_controller.h"
#include "audio_player_editor.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/level.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "log/logger.h"

namespace kpengine::audio_player
{
    AudioPlayerHost::AudioPlayerHost() = default;

    AudioPlayerHost::~AudioPlayerHost()
    {
        Shutdown();
    }

    bool AudioPlayerHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::AudioPlayer)
        {
            diagnostic = "AudioPlayerHost can only initialize in audio-player mode";
            return false;
        }

        audio_system_ = std::make_unique<audio::MiniAudioSystem>();
        controller_ = std::make_unique<AudioPlayerController>(*audio_system_);
        runtime::global_runtime_context.InitializeSceneServices();
        const asset::AssetID error_material = asset::AssetManager::GetInstance().LoadSync(
            GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath);
        if (!error_material.IsValid())
        {
            diagnostic = "Audio Player could not load the Runtime error material root";
            return false;
        }
        render_asset_roots_ = {error_material};
        std::string refresh_diagnostic;
        if (!controller_->RefreshProjectLibrary(refresh_diagnostic))
            KP_LOG("Audio", LOG_LEVEL_WARNING, "Could not refresh project music: %s",
                   refresh_diagnostic.c_str());
        initialized_ = true;
        KP_LOG("Audio", LOG_LEVEL_INFO, "Standalone Audio Player initialized");
        return true;
    }

    bool AudioPlayerHost::InitializePresentation(runtime::Engine &engine,
                                                std::string &diagnostic)
    {
        if (!initialized_ || audio_system_ == nullptr || controller_ == nullptr)
        {
            diagnostic = "Audio Player runtime services are unavailable";
            return false;
        }
        player_editor_ = std::make_unique<AudioPlayerEditor>();
        if (!player_editor_->Initialize(engine, *audio_system_, *controller_, diagnostic))
        {
            player_editor_.reset();
            return false;
        }
        return true;
    }

    bool AudioPlayerHost::Tick(float, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_ || controller_ == nullptr)
        {
            diagnostic = "Audio Player host is not initialized";
            return false;
        }
        controller_->Tick();
        return true;
    }

    bool AudioPlayerHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        return initialized_;
    }

    bool AudioPlayerHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        using namespace runtime::command;
        if (controller_ == nullptr)
        {
            diagnostic = "Audio Player controller is unavailable";
            return false;
        }

        const CommandFlags mutate = CommandFlags::AgentAllowed | CommandFlags::MutatesState;
        const auto install = [this, &registry, &diagnostic](CommandDesc descriptor)
        {
            CommandRegistrationResult result = registry.Register(std::move(descriptor));
            if (!result.IsSuccess())
            {
                diagnostic = result.diagnostic;
                return false;
            }
            command_registrations_.push_back(std::move(result.registration));
            return true;
        };
        const auto register_action = [&](const char *name, const char *help,
            std::function<bool(AudioPlayerController &, std::string &)> operation)
        {
            return install({name, "AudioPlayer", help, CommandCategory::Engine, mutate, {},
                [this, operation](const CommandCall &, const CommandContext &context)
                {
                    std::string message;
                    const bool succeeded = operation(*controller_, message);
                    return CommandResult{succeeded ? CommandStatus::Success : CommandStatus::Failed,
                        succeeded ? "Audio command completed" : message, context.request_id, {}};
                }, CommandThread::Game});
        };
        const auto register_value = [&](const char *name, const char *help,
            CommandArgumentDesc argument,
            std::function<bool(AudioPlayerController &, const CommandCall &, std::string &)> operation)
        {
            return install({name, "AudioPlayer", help, CommandCategory::Engine, mutate,
                {{std::move(argument)}},
                [this, operation](const CommandCall &call, const CommandContext &context)
                {
                    std::string message;
                    const bool succeeded = operation(*controller_, call, message);
                    return CommandResult{succeeded ? CommandStatus::Success : CommandStatus::Failed,
                        succeeded ? "Audio setting applied" : message, context.request_id, {}};
                }, CommandThread::Game});
        };

        CommandDesc import_audio{};
        import_audio.name = "audio.import_file";
        import_audio.provider = "AudioPlayer";
        import_audio.help = "Import a local WAV, MP3, or FLAC file; optionally attach an SRT, WebVTT, or LRC subtitle";
        import_audio.category = CommandCategory::Engine;
        import_audio.flags = mutate;
        import_audio.schema.arguments = {
            {"path", CommandValueType::String, true, {}, {}},
            {"subtitle", CommandValueType::String, false, {}, {}}};
        import_audio.handler = [this](const CommandCall &call, const CommandContext &context)
        {
            const auto subtitle = call.arguments.find("subtitle");
            const std::string subtitle_path = subtitle == call.arguments.end()
                ? std::string{} : std::get<std::string>(subtitle->second);
            std::string message;
            const bool succeeded = controller_->ImportFile(
                std::get<std::string>(call.arguments.at("path")), subtitle_path, message);
            return CommandResult{succeeded ? CommandStatus::Success : CommandStatus::Failed,
                succeeded ? "Audio import queued" : message, context.request_id, {}};
        };
        import_audio.execution_thread = CommandThread::Game;
        if (!install(std::move(import_audio)) ||
            !register_value("audio.import_folder", "Queue supported audio files from one folder",
                {"path", CommandValueType::String, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &message)
                { return controller.ImportFolder(std::get<std::string>(call.arguments.at("path")), message); }))
        {
            return false;
        }

        if (!register_action("audio.play", "Start or resume the selected track",
                [](AudioPlayerController &controller, std::string &message)
                { return controller.PlaySelected(message); }) ||
            !register_action("audio.toggle", "Toggle play and pause for the selected track",
                [](AudioPlayerController &controller, std::string &message)
                { return controller.TogglePlayPause(message); }) ||
            !register_action("audio.pause", "Pause the selected track",
                [](AudioPlayerController &controller, std::string &)
                { controller.Pause(); return true; }) ||
            !register_action("audio.stop", "Stop the selected track",
                [](AudioPlayerController &controller, std::string &)
                { controller.Stop(); return true; }) ||
            !register_action("audio.next", "Select and play the next queued track",
                [](AudioPlayerController &controller, std::string &message)
                { return controller.Next(true, message); }) ||
            !register_action("audio.previous", "Replay or select the previous queued track",
                [](AudioPlayerController &controller, std::string &message)
                { return controller.Previous(true, message); }) ||
            !register_action("audio.remove_selected", "Remove the selected item from the session queue",
                [](AudioPlayerController &controller, std::string &message)
                { if (!controller.RemoveSelected()) { message = "No selected track"; return false; } return true; }))
        {
            return false;
        }

        if (!register_value("audio.select", "Select a queued track by stable queue ID",
                {"track_id", CommandValueType::UnsignedInteger, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &message)
                {
                    if (!controller.Select(std::get<std::uint64_t>(call.arguments.at("track_id"))))
                    { message = "Track ID is not in the queue"; return false; }
                    return true;
                }) ||
            !register_value("audio.seek", "Seek the active buffered track in seconds",
                {"seconds", CommandValueType::Float, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &message)
                {
                    if (!controller.Seek(static_cast<float>(std::get<double>(call.arguments.at("seconds")))))
                    { message = "No seekable track is active"; return false; }
                    return true;
                }) ||
            !register_value("audio.speed", "Set playback speed from 0.5x to 2.0x",
                {"value", CommandValueType::Float, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &message)
                {
                    if (!controller.SetPlaybackRate(static_cast<float>(
                            std::get<double>(call.arguments.at("value")))))
                    { message = "Playback speed must be between 0.5x and 2.0x"; return false; }
                    return true;
                }) ||
            !register_value("audio.volume", "Set selected-track volume from 0 to 1",
                {"value", CommandValueType::Float, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &)
                { controller.SetVolume(static_cast<float>(std::get<double>(call.arguments.at("value")))); return true; }) ||
            !register_value("audio.mute", "Mute or unmute selected-track output",
                {"enabled", CommandValueType::Boolean, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &)
                { controller.SetMuted(std::get<bool>(call.arguments.at("enabled"))); return true; }) ||
            !register_value("audio.loop", "Loop only the selected track",
                {"enabled", CommandValueType::Boolean, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &)
                { controller.SetLoopTrack(std::get<bool>(call.arguments.at("enabled"))); return true; }) ||
            !register_value("audio.shuffle", "Enable or disable queue shuffle",
                {"enabled", CommandValueType::Boolean, true, {}, {}},
                [](AudioPlayerController &controller, const CommandCall &call, std::string &)
                { controller.SetShuffle(std::get<bool>(call.arguments.at("enabled"))); return true; }))
        {
            return false;
        }

        CommandDesc clear{
            "audio.clear", "AudioPlayer", "Stop playback and clear the session queue",
            CommandCategory::Engine, mutate | CommandFlags::Destructive, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                controller_->ClearQueue();
                return CommandResult{CommandStatus::Success, "Audio queue cleared",
                                     context.request_id, {}};
            }, CommandThread::Game};
        if (!install(std::move(clear)))
        {
            return false;
        }

        CommandDesc status{
            "audio.status", "AudioPlayer", "Read selected track, queue, and playback state",
            CommandCategory::Engine, CommandFlags::AgentAllowed, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                const PlaybackView view = controller_->GetPlaybackView();
                const std::string track = view.track.has_value() ? view.track->name : std::string{};
                const std::uint64_t track_id = view.track.has_value() ? view.track->id : 0;
                return CommandResult{CommandStatus::Success, view.status, context.request_id,
                    {{"track", track}, {"track_id", track_id},
                     {"state", static_cast<std::uint64_t>(view.state)},
                     {"elapsed_seconds", static_cast<double>(view.position_seconds)},
                     {"duration_seconds", static_cast<double>(view.duration_seconds)},
                     {"playback_rate", static_cast<double>(view.playback_rate)},
                     {"queue_size", static_cast<std::uint64_t>(view.queue_size)},
                     {"pending_imports", static_cast<std::uint64_t>(view.pending_imports)},
                     {"subtitle_attached", view.subtitle_track_attached},
                     {"subtitle_language", view.subtitle_language},
                     {"subtitle_text", view.subtitle_text},
                     {"error", view.error}}};
            }, CommandThread::Immediate};
        if (!install(std::move(status)))
        {
            return false;
        }
        diagnostic.clear();
        return true;
    }

    bool AudioPlayerHost::RenderPresentation(std::string &diagnostic)
    {
        if (player_editor_ == nullptr)
        {
            diagnostic = "Audio Player UI is unavailable";
            return false;
        }
        return player_editor_->Render(diagnostic);
    }

    void AudioPlayerHost::ShutdownRenderThread() noexcept
    {
        if (player_editor_ != nullptr)
        {
            player_editor_->Shutdown();
            player_editor_.reset();
        }
    }

    void AudioPlayerHost::Shutdown() noexcept
    {
        ShutdownRenderThread();
        command_registrations_.clear();
        if (controller_ != nullptr)
        {
            controller_->Shutdown();
            controller_.reset();
        }
        if (audio_system_ != nullptr)
        {
            audio_system_->ShutDown();
            audio_system_.reset();
        }
        initialized_ = false;
    }
}
