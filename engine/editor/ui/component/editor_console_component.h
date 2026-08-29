#ifndef KPENGINE_EDITOR_CONSOLE_COMPONENT_H
#define KPENGINE_EDITOR_CONSOLE_COMPONENT_H

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "editor/ui/component/editor_ui_component.h"
#include "runtime/command/command_registry.h"
#include "runtime/input/input_system.h"

namespace kpengine::editor
{
    class EditorConsoleComponent final : public EditorUIComponent
    {
    public:
        EditorConsoleComponent(runtime::command::CommandRegistry *registry,
                               input::InputSystem *input_system);
        ~EditorConsoleComponent() override;

        void Render() override;

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
        input::InputSystem::KeyListenerHandle key_listener_handle_ = 0;
        std::shared_ptr<ConsoleState> state_;

        bool is_open_ = false;
        bool focus_input_ = false;
        int history_cursor_ = -1;
        std::array<char, 1024> input_buffer_{};
        std::deque<std::string> history_;
        std::deque<std::string> output_;
        std::vector<std::string> completion_candidates_;
    };
}

#endif
