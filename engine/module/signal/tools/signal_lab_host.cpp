#include "signal_lab_host.h"

#include "signal_lab_editor.h"
#include "signal_scope_session.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/level.h"
#include "runtime/core/config/path.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"

#include <numbers>
#include <utility>

namespace kpengine::signal
{
    SignalLabHost::SignalLabHost() = default;

    SignalLabHost::~SignalLabHost()
    {
        Shutdown();
    }

    bool SignalLabHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != "signal-lab")
        {
            diagnostic = "SignalLabHost can only initialize in signal-lab mode";
            return false;
        }
        scope_session_ = std::make_shared<SignalScopeSession>();
        runtime::global_runtime_context.InitializeSceneServices();
        const asset::AssetID error_material = asset::AssetManager::GetInstance().LoadSync(
            GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath);
        if (!error_material.IsValid())
        {
            diagnostic = "Signal Lab could not load the Runtime error material root";
            return false;
        }
        render_asset_roots_ = {error_material};
        initialized_ = true;
        return true;
    }

    bool SignalLabHost::InitializePresentation(runtime::Engine &engine,
                                              std::string &diagnostic)
    {
        if (!initialized_)
        {
            diagnostic = "Signal Lab host is not initialized";
            return false;
        }
        editor_ = std::make_unique<SignalLabEditor>();
        if (!editor_->Initialize(engine, scope_session_, diagnostic))
        {
            editor_.reset();
            return false;
        }
        return true;
    }

    bool SignalLabHost::Tick(const float delta_time, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!initialized_)
        {
            diagnostic = "Signal Lab host is not initialized";
            return false;
        }
        return scope_session_ != nullptr && scope_session_->Tick(delta_time, diagnostic);
    }

    bool SignalLabHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        return initialized_;
    }

    bool SignalLabHost::RenderPresentation(std::string &diagnostic)
    {
        if (editor_ == nullptr)
        {
            diagnostic = "Signal Lab presentation is unavailable";
            return false;
        }
        return editor_->Render(diagnostic);
    }

    bool SignalLabHost::RegisterHostCommands(runtime::command::CommandRegistry &registry,
                                             std::string &diagnostic)
    {
        using namespace runtime::command;
        diagnostic.clear();
        if (scope_session_ == nullptr)
        {
            diagnostic = "Signal Lab acquisition session is unavailable";
            return false;
        }
        const auto install = [this, &diagnostic, &registry](CommandDesc descriptor)
        {
            auto result = registry.Register(std::move(descriptor));
            if (!result.IsSuccess())
            {
                diagnostic = result.diagnostic;
                return false;
            }
            command_registrations_.push_back(std::move(result.registration));
            return true;
        };
        const CommandFlags mutate = CommandFlags::AgentAllowed | CommandFlags::MutatesState;
        const auto register_action = [&](const char *name, const char *help,
            std::function<bool(std::string &)> operation)
        {
            return install({name, "SignalLab", help, CommandCategory::Engine, mutate, {},
                [operation](const CommandCall &, const CommandContext &context)
                {
                    std::string message;
                    const bool succeeded = operation(message);
                    return CommandResult{succeeded ? CommandStatus::Success
                                                   : CommandStatus::Failed,
                        succeeded ? "Signal command completed" : message,
                        context.request_id, {}};
                }, CommandThread::Game});
        };
        if (!register_action("signal.run", "Start simulated acquisition",
                [this](std::string &)
                { scope_session_->Run(); return true; }) ||
            !register_action("signal.pause", "Pause without clearing acquisition state",
                [this](std::string &)
                { scope_session_->Pause(); return true; }) ||
            !register_action("signal.reset", "Clear sample history and reset source phase",
                [this](std::string &message)
                { return scope_session_->Reset(message); }) ||
            !register_action("signal.step", "Generate one 256-sample block while paused",
                [this](std::string &message)
                { return scope_session_->Step(message); }) ||
            !register_action("signal.arm", "Re-arm a Single trigger capture",
                [this](std::string &message)
                { return scope_session_->Arm(message); }))
        {
            return false;
        }

        CommandDesc configure{};
        configure.name = "signal.configure";
        configure.provider = "SignalLab";
        configure.help = "Set simulated waveform, harmonic level and noise RMS";
        configure.category = CommandCategory::Engine;
        configure.flags = mutate;
        configure.schema.arguments = {
            {"waveform", CommandValueType::Enum, true, {},
             {"sine", "cosine", "square", "constant"}},
            {"sample_rate_hz", CommandValueType::Float, true, {}, {}},
            {"frequency_hz", CommandValueType::Float, true, {}, {}},
            {"amplitude", CommandValueType::Float, true, {}, {}},
            {"phase_degrees", CommandValueType::Float, true, {}, {}},
            {"offset", CommandValueType::Float, true, {}, {}},
            {"harmonic_distortion", CommandValueType::Float, false, {}, {}},
            {"noise_rms", CommandValueType::Float, false, {}, {}},
            {"seed", CommandValueType::UnsignedInteger, false, {}, {}}};
        configure.handler = [this](const CommandCall &call, const CommandContext &context)
        {
            SignalGeneratorConfig source;
            const std::string &waveform = std::get<std::string>(call.arguments.at("waveform"));
            source.kind = waveform == "cosine" ? SignalKind::Cosine
                : waveform == "square" ? SignalKind::Square
                : waveform == "constant" ? SignalKind::Constant : SignalKind::Sine;
            source.sample_rate_hz = std::get<double>(call.arguments.at("sample_rate_hz"));
            source.frequency_hz = std::get<double>(call.arguments.at("frequency_hz"));
            source.amplitude = std::get<double>(call.arguments.at("amplitude"));
            source.phase_radians = std::get<double>(call.arguments.at("phase_degrees")) *
                                   (std::numbers::pi_v<double> / 180.0);
            source.offset = std::get<double>(call.arguments.at("offset"));
            if (const auto harmonic = call.arguments.find("harmonic_distortion");
                harmonic != call.arguments.end())
            {
                source.harmonic_distortion = std::get<double>(harmonic->second);
            }
            if (const auto noise = call.arguments.find("noise_rms");
                noise != call.arguments.end())
            {
                source.noise_rms = std::get<double>(noise->second);
            }
            if (const auto seed = call.arguments.find("seed"); seed != call.arguments.end())
            {
                source.seed = std::get<std::uint64_t>(seed->second);
            }
            if (source.kind == SignalKind::Constant)
            {
                source.constant_value = source.offset + source.amplitude;
                source.offset = 0.0;
            }
            source.unit = SampleUnit::Volt;
            std::string message;
            const bool succeeded = scope_session_->SetSource(source, message);
            if (succeeded)
            {
                scope_session_->Run();
            }
            return CommandResult{succeeded ? CommandStatus::Success : CommandStatus::Failed,
                succeeded ? "Signal source configured" : message,
                context.request_id, {}};
        };
        configure.execution_thread = CommandThread::Game;
        if (!install(std::move(configure)))
        {
            return false;
        }

        CommandDesc status{};
        status.name = "signal.status";
        status.provider = "SignalLab";
        status.help = "Read sample position, current capture and measured values";
        status.category = CommandCategory::Engine;
        status.flags = CommandFlags::AgentAllowed;
        status.handler = [this](const CommandCall &, const CommandContext &context)
        {
            const auto snapshot = scope_session_->Snapshot();
            if (snapshot == nullptr)
            {
                return CommandResult{CommandStatus::Failed, "Signal snapshot is unavailable",
                                     context.request_id, {}};
            }
            const std::string transport = snapshot->transport == ScopeTransport::Running
                ? "running" : snapshot->transport == ScopeTransport::Paused ? "paused" : "stopped";
            return CommandResult{CommandStatus::Success, snapshot->state_label,
                context.request_id,
                {{"transport", transport},
                 {"sample_position", snapshot->sample_position},
                 {"epoch", snapshot->epoch},
                 {"capture_id", snapshot->capture_id},
                 {"has_capture", snapshot->capture != nullptr},
                 {"stale", snapshot->stale},
                 {"triggered", snapshot->trigger_sample.has_value()},
                 {"trigger_sample", snapshot->trigger_sample.value_or(0)},
                 {"sample_rate_hz", snapshot->source.sample_rate_hz},
                 {"harmonic_distortion", snapshot->source.harmonic_distortion},
                 {"noise_rms", snapshot->source.noise_rms},
                 {"timebase_ms", snapshot->timebase_ms},
                 {"capture_size", static_cast<std::uint64_t>(snapshot->capture_size)},
                 {"peak_to_peak", snapshot->measurements.peak_to_peak},
                 {"rms", snapshot->measurements.rms},
                 {"frequency_available", snapshot->frequency_available},
                 {"frequency_hz", snapshot->measured_frequency_hz},
                 {"frequency_uncertainty_hz",
                  snapshot->measured_frequency_uncertainty_hz},
                 {"holdoff_samples", snapshot->holdoff_samples}}};
        };
        if (!install(std::move(status)))
        {
            return false;
        }
        diagnostic.clear();
        return true;
    }

    void SignalLabHost::ShutdownRenderThread() noexcept
    {
        if (editor_ != nullptr)
        {
            editor_->Shutdown();
            editor_.reset();
        }
    }

    void SignalLabHost::Shutdown() noexcept
    {
        ShutdownRenderThread();
        command_registrations_.clear();
        render_asset_roots_.clear();
        scope_session_.reset();
        initialized_ = false;
    }
}
