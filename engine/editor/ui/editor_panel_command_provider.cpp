#include "editor/ui/editor_panel_command_provider.h"

#include <utility>
#include <variant>

namespace kpengine::editor
{
    namespace
    {
        constexpr const char *kOwner = "EditorPanel";

        runtime::command::CommandResult QueueRequest(
            const runtime::command::CommandContext &context,
            const EditorPanelCommandEnqueue &enqueue, EditorPanelCommandRequest request)
        {
            if (!context.complete)
            {
                return {runtime::command::CommandStatus::Failed,
                        "Editor panel command requires deferred completion", context.request_id,
                        {}};
            }
            if (!enqueue(std::move(request)))
            {
                return {runtime::command::CommandStatus::Failed,
                        "Editor panel command queue is unavailable", context.request_id, {}};
            }
            return {runtime::command::CommandStatus::Pending,
                    "Editor panel command queued", context.request_id, {}};
        }

        runtime::command::CommandRegistrationResult Register(
            runtime::command::CommandRegistry &registry, runtime::command::CommandDesc descriptor)
        {
            return registry.Register(std::move(descriptor));
        }
    }

    EditorPanelCommandRegistrationResult RegisterEditorPanelCommands(
        runtime::command::CommandRegistry &registry, EditorPanelCommandEnqueue enqueue)
    {
        EditorPanelCommandRegistrationResult result;
        if (!enqueue)
        {
            result.diagnostic = "editor panel command provider requires an enqueue sink";
            return result;
        }

        const runtime::command::CommandFlags read_flags =
            runtime::command::CommandFlags::AgentAllowed |
            runtime::command::CommandFlags::LuaAllowed;
        const runtime::command::CommandFlags write_flags =
            runtime::command::CommandFlags::AgentAllowed |
            runtime::command::CommandFlags::MutatesState;

        runtime::command::CommandRegistrationResult registration = Register(
            registry,
            {"editor.panel.list", kOwner, "List editor panels and their current placement",
             runtime::command::CommandCategory::Debug, read_flags, {},
             [enqueue](const runtime::command::CommandCall &,
                       const runtime::command::CommandContext &context)
             {
                 return QueueRequest(context, enqueue,
                                      {EditorPanelCommandKind::List, {}, context.complete});
             },
             runtime::command::CommandThread::Game});
        if (!registration.IsSuccess())
        {
            result.diagnostic = registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(registration.registration));

        const auto register_panel_command = [&](const char *name, const char *help,
                                                 EditorPanelCommandKind kind,
                                                 runtime::command::CommandFlags flags)
        {
            return Register(
                registry,
                runtime::command::CommandDesc{
                 name, kOwner, help, runtime::command::CommandCategory::Debug, flags,
                 runtime::command::CommandSchema{
                     {{"id", runtime::command::CommandValueType::String, true, {}, {}}}},
                 [enqueue, kind](const runtime::command::CommandCall &call,
                                 const runtime::command::CommandContext &context)
                 {
                     const auto iterator = call.arguments.find("id");
                     const auto *const id = iterator == call.arguments.end()
                                                ? nullptr
                                                : std::get_if<std::string>(&iterator->second);
                     if (id == nullptr || id->empty())
                     {
                         return runtime::command::CommandResult{
                             runtime::command::CommandStatus::InvalidArguments,
                             "editor panel command requires a non-empty id",
                             context.request_id, {}};
                     }
                     return QueueRequest(context, enqueue, {kind, *id, context.complete});
                 },
                 runtime::command::CommandThread::Game});
        };

        registration = register_panel_command(
            "editor.panel.show", "Open and activate an editor panel", EditorPanelCommandKind::Show,
            write_flags);
        if (!registration.IsSuccess())
        {
            result.registrations.clear();
            result.diagnostic = registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(registration.registration));

        registration = register_panel_command(
            "editor.panel.focus", "Activate an open editor panel", EditorPanelCommandKind::Focus,
            write_flags);
        if (!registration.IsSuccess())
        {
            result.registrations.clear();
            result.diagnostic = registration.diagnostic;
            return result;
        }
        result.registrations.push_back(std::move(registration.registration));

        result.succeeded = true;
        return result;
    }
}
