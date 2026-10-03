#include "tts_editor_host.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "tts_editor_controller.h"
#include "tts_editor_editor.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/level.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"

namespace kpengine::tts_editor
{
    TtsEditorHost::TtsEditorHost() = default;
    TtsEditorHost::~TtsEditorHost() { Shutdown(); }

    bool TtsEditorHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::TtsEditor)
        {
            diagnostic = "TtsEditorHost requires tts mode";
            return false;
        }
        controller_ = std::make_unique<TtsEditorController>(TtsSettingsPath());
        controller_->Initialize(diagnostic);
        runtime::global_runtime_context.InitializeSceneServices();
        const asset::AssetID error_material = asset::AssetManager::GetInstance().LoadSync(
            GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath);
        if (!error_material.IsValid())
        {
            diagnostic = "TTS editor could not load the Runtime error material root";
            return false;
        }
        render_asset_roots_ = {error_material};
        initialized_ = true;
        diagnostic.clear();
        return true;
    }

    bool TtsEditorHost::InitializePresentation(runtime::Engine &engine,
                                                std::string &diagnostic)
    {
        if (!initialized_ || !controller_)
        {
            diagnostic = "TTS editor controller is unavailable";
            return false;
        }
        editor_ = std::make_unique<TtsEditorEditor>();
        if (!editor_->Initialize(engine, *controller_, diagnostic))
        {
            editor_.reset();
            return false;
        }
        return true;
    }

    bool TtsEditorHost::Tick(float, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_ || !controller_)
        {
            diagnostic = "TTS editor is not initialized";
            return false;
        }
        controller_->Tick();
        return true;
    }

    bool TtsEditorHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        return initialized_;
    }

    bool TtsEditorHost::RenderPresentation(std::string &diagnostic)
    {
        if (!editor_)
        {
            diagnostic = "TTS editor presentation is unavailable";
            return false;
        }
        return editor_->Render(diagnostic);
    }

    bool TtsEditorHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        using namespace runtime::command;
        if (!controller_)
        {
            diagnostic = "TTS editor controller is unavailable";
            return false;
        }
        const auto install = [this, &registry, &diagnostic](CommandDesc command)
        {
            CommandRegistrationResult result = registry.Register(std::move(command));
            if (!result.IsSuccess())
            {
                diagnostic = result.diagnostic;
                return false;
            }
            command_registrations_.push_back(std::move(result.registration));
            return true;
        };
        CommandDesc status{
            "tts.status", "TtsEditor", "Read TTS job and playback state",
            CommandCategory::Engine, CommandFlags::AgentAllowed, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                const auto view = controller_->GetView();
                const auto selected = std::find_if(view.entries.begin(), view.entries.end(),
                    [&view](const TtsEntryView &entry) { return entry.id == view.selected_id; });
                const bool has_selected = selected != view.entries.end();
                return CommandResult{CommandStatus::Success, view.status, context.request_id,
                    {{"settings_loaded", view.settings_loaded},
                     {"configured", view.can_generate},
                     {"playback_mode", view.settings.streaming ? "stream" : "buffer"},
                     {"selected_playback_mode", has_selected ?
                         (selected->streaming ? "stream" : "buffer") : ""},
                     {"entry_count", static_cast<std::uint64_t>(view.entries.size())},
                     {"library_loaded", view.library_loaded},
                     {"selected_durable", has_selected && selected->durable},
                     {"last_export_path", view.last_export_path},
                     {"selected_id", view.selected_id},
                     {"job_state", has_selected ? static_cast<std::uint64_t>(selected->state) : 0u},
                     {"audio_state", static_cast<std::uint64_t>(view.audio_state)},
                     {"elapsed_seconds", static_cast<double>(view.elapsed_seconds)},
                     {"error", !view.error.empty() ? view.error :
                         has_selected ? selected->error : std::string{}},
                     {"generation_blocker", view.generation_blocker}}};
            }, CommandThread::Immediate};
        CommandDesc generate{
            "tts.generate", "TtsEditor", "Queue speech for the configured voice",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {{{"text", CommandValueType::String, true, {}, {}}}},
            [this](const CommandCall &call, const CommandContext &context)
            {
                controller_->QueueGenerate(std::get<std::string>(call.arguments.at("text")));
                return CommandResult{CommandStatus::Success, "TTS request queued",
                                     context.request_id, {}};
            }, CommandThread::Game};
        CommandDesc settings{
            "tts.settings", "TtsEditor", "Inspect configured TTS connection and voice selection",
            CommandCategory::Engine, CommandFlags::AgentAllowed, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                const auto view = controller_->GetView();
                const TtsVoicePreset *voice = SelectedVoice(view.settings);
                return CommandResult{CommandStatus::Success,
                    view.settings_loaded ? "TTS settings loaded" : "TTS settings need repair",
                    context.request_id,
                    {{"settings_loaded", view.settings_loaded},
                     {"address", view.settings.address},
                     {"port", static_cast<std::uint64_t>(view.settings.port)},
                     {"api_path", view.settings.api_path},
                     {"playback_mode", view.settings.streaming ? "stream" : "buffer"},
                     {"voice_count", static_cast<std::uint64_t>(view.settings.voices.size())},
                     {"selected_voice", voice ? voice->name : std::string{}},
                     {"reference_audio_configured", voice && !voice->ref_audio_path.empty()},
                     {"reference_text_configured", voice && !voice->ref_text.empty()},
                     {"generation_blocker", view.generation_blocker}}};
            }, CommandThread::Immediate};
        CommandDesc cancel{
            "tts.cancel", "TtsEditor", "Cancel the selected speech job",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                controller_->QueueCancel();
                return CommandResult{CommandStatus::Success, "TTS cancellation queued",
                                     context.request_id, {}};
            }, CommandThread::Game};
        CommandDesc import_wav{
            "tts.import", "TtsEditor", "Import and own a local PCM16 WAV",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {{{"path", CommandValueType::String, true, {}, {}}}},
            [this](const CommandCall &call, const CommandContext &context)
            {
                controller_->QueueImport(std::get<std::string>(call.arguments.at("path")));
                return CommandResult{CommandStatus::Success, "WAV import queued", context.request_id, {}};
            }, CommandThread::Game};
        CommandDesc duplicate{
            "tts.duplicate", "TtsEditor", "Duplicate the selected saved dialog",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                controller_->QueueDuplicate();
                return CommandResult{CommandStatus::Success, "Dialog duplication queued", context.request_id, {}};
            }, CommandThread::Game};
        CommandDesc delete_dialog{
            "tts.delete", "TtsEditor", "Delete the selected saved dialog",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState, {},
            [this](const CommandCall &, const CommandContext &context)
            {
                controller_->QueueDelete();
                return CommandResult{CommandStatus::Success, "Dialog deletion queued", context.request_id, {}};
            }, CommandThread::Game};
        CommandDesc export_wav{
            "tts.export", "TtsEditor", "Export the selected saved dialog as WAV",
            CommandCategory::Engine,
            CommandFlags::AgentAllowed | CommandFlags::MutatesState,
            {{{"basename", CommandValueType::String, true, {}, {}}}},
            [this](const CommandCall &call, const CommandContext &context)
            {
                controller_->QueueExport(std::get<std::string>(call.arguments.at("basename")));
                return CommandResult{CommandStatus::Success, "WAV export queued", context.request_id, {}};
            }, CommandThread::Game};
        if (!install(std::move(status)) || !install(std::move(settings)) ||
            !install(std::move(generate)) ||
            !install(std::move(cancel)) || !install(std::move(import_wav)) ||
            !install(std::move(duplicate)) || !install(std::move(delete_dialog)) ||
            !install(std::move(export_wav)))
            return false;
        diagnostic.clear();
        return true;
    }

    void TtsEditorHost::ShutdownRenderThread() noexcept
    {
        if (editor_)
        {
            editor_->Shutdown();
            editor_.reset();
        }
    }

    void TtsEditorHost::Shutdown() noexcept
    {
        ShutdownRenderThread();
        command_registrations_.clear();
        if (controller_)
        {
            controller_->Shutdown();
            controller_.reset();
        }
        render_asset_roots_.clear();
        initialized_ = false;
    }
}
