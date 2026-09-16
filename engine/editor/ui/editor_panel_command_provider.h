#ifndef KPENGINE_EDITOR_PANEL_COMMAND_PROVIDER_H
#define KPENGINE_EDITOR_PANEL_COMMAND_PROVIDER_H

#include <functional>
#include <string>
#include <vector>

#include "command/command_registry.h"

namespace kpengine::editor
{
    enum class EditorPanelCommandKind
    {
        List,
        Show,
        Focus,
    };

    struct EditorPanelCommandRequest final
    {
        EditorPanelCommandKind kind = EditorPanelCommandKind::List;
        std::string id;
        runtime::command::CommandCompletionSink completion;
    };

    using EditorPanelCommandEnqueue =
        std::function<bool(EditorPanelCommandRequest)>;

    struct EditorPanelCommandRegistrationResult final
    {
        bool succeeded = false;
        std::string diagnostic;
        std::vector<runtime::command::CommandRegistration> registrations;
    };

    EditorPanelCommandRegistrationResult RegisterEditorPanelCommands(
        runtime::command::CommandRegistry &registry, EditorPanelCommandEnqueue enqueue);
}

#endif
