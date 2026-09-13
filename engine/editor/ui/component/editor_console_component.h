#ifndef KPENGINE_EDITOR_CONSOLE_COMPONENT_H
#define KPENGINE_EDITOR_CONSOLE_COMPONENT_H

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "editor/ui/component/editor_window_component.h"
#include "runtime/command/command_registry.h"
#include "runtime/input/input_system.h"

struct ImFont;

namespace kpengine::editor
{
    // A window component so the tool row can host it as a tab: the row supplies
    // Begin()/End() and owns visibility, and this class only draws its body.
    class EditorConsoleComponent final : public EditorWindowComponent
    {
    public:
        EditorConsoleComponent(runtime::command::CommandRegistry *registry,
                               input::InputSystem *input_system, ImFont *code_font = nullptr);
        ~EditorConsoleComponent() override;

        // Per-frame upkeep without drawing. The tool row calls this for a console
        // whose tab is closed, so deferred results are still drained while hidden —
        // which the old Render() did unconditionally at its top.
        void Pump();

        void RenderContent() override;

    private:
        struct ConsoleState;

        static int InputCallback(ImGuiInputTextCallbackData *data);
        void HandleKeyEvent(const KeyEvent &event);
        void Submit();
        void ReplaceCurrentToken(const std::string &candidate);
        void DrainCompletions();
        void AppendResult(const runtime::command::CommandResult &result);
        void AppendOutput(std::string text);

        runtime::command::CommandRegistry *registry_ = nullptr;
        input::InputSystem *input_system_ = nullptr;
        ImFont *code_font_ = nullptr;
        input::InputSystem::KeyListenerHandle key_listener_handle_ = 0;
        std::shared_ptr<ConsoleState> state_;

        bool focus_input_ = false;
        int history_cursor_ = -1;
        std::array<char, 1024> input_buffer_{};
        std::deque<std::string> history_;
        std::deque<std::string> output_;
        std::vector<std::string> completion_candidates_;
    };
}

#endif
