#ifndef KPENGINE_EDITOR_UI_SESSION_COMMANDS_H
#define KPENGINE_EDITOR_UI_SESSION_COMMANDS_H

#include <string>
#include <string_view>
#include <functional>
#include <utility>

#include "command/command_registry.h"
#include "editor/ui/api/editor_ui/ui_snapshot.h"

namespace kpengine::editor
{
    class EditorUiSessionRegistration final
    {
    public:
        EditorUiSessionRegistration() = default;
        ~EditorUiSessionRegistration();
        EditorUiSessionRegistration(const EditorUiSessionRegistration &) = delete;
        EditorUiSessionRegistration &operator=(const EditorUiSessionRegistration &) = delete;
        EditorUiSessionRegistration(EditorUiSessionRegistration &&other) noexcept;
        EditorUiSessionRegistration &operator=(EditorUiSessionRegistration &&other) noexcept;
        explicit operator bool() const noexcept { return static_cast<bool>(release_); }

    private:
        friend EditorUiSessionRegistration RegisterEditorUiSession(
            runtime::command::CommandRegistry &, ui::UiSnapshotStore &, ui::UiSessionId,
            std::string, std::string, std::string &);
        explicit EditorUiSessionRegistration(std::function<void()> release)
            : release_(std::move(release)) {}
        void Reset() noexcept;
        std::function<void()> release_;
    };

    EditorUiSessionRegistration RegisterEditorUiSession(
        runtime::command::CommandRegistry &registry,
        ui::UiSnapshotStore &store,
        ui::UiSessionId session,
        std::string host,
        std::string mode,
        std::string &diagnostic);
}

#endif
