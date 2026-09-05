#include "editor/ui/component/editor_console_component.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <glfw/glfw3.h>
#include <imgui.h>

#include "runtime/command/command_parser.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr size_t kMaxHistoryEntries = 64;
        constexpr size_t kMaxOutputEntries = 128;

        const char *StatusName(const runtime::command::CommandStatus status)
        {
            using runtime::command::CommandStatus;
            switch (status)
            {
            case CommandStatus::Success:
                return "success";
            case CommandStatus::InvalidArguments:
                return "invalid arguments";
            case CommandStatus::NotFound:
                return "not found";
            case CommandStatus::Denied:
                return "denied";
            case CommandStatus::Busy:
                return "busy";
            case CommandStatus::Pending:
                return "pending";
            case CommandStatus::Failed:
                return "failed";
            case CommandStatus::Cancelled:
                return "cancelled";
            case CommandStatus::Shutdown:
                return "shutdown";
            case CommandStatus::WrongThread:
                return "wrong thread";
            }
            return "unknown";
        }

        const std::string *FindString(const runtime::command::CommandData &data,
                                      const char *name)
        {
            const auto iterator = data.find(name);
            return iterator == data.end() ? nullptr
                                          : std::get_if<std::string>(&iterator->second);
        }

        void SetBuffer(ImGuiInputTextCallbackData *data, const std::string &value)
        {
            const size_t max_length = static_cast<size_t>(data->BufSize - 1);
            const size_t length = std::min(max_length, value.size());
            std::memcpy(data->Buf, value.data(), length);
            data->Buf[length] = '\0';
            data->BufTextLen = static_cast<int>(length);
            data->CursorPos = data->BufTextLen;
            data->SelectionStart = data->SelectionEnd = data->CursorPos;
        }
    }

    struct EditorConsoleComponent::ConsoleState
    {
        std::mutex mutex;
        std::deque<runtime::command::CommandResult> completed_results;
    };

    EditorConsoleComponent::EditorConsoleComponent(runtime::command::CommandRegistry *registry,
                                                   input::InputSystem *input_system,
                                                   ImFont *code_font)
        : registry_(registry), input_system_(input_system), code_font_(code_font),
          state_(std::make_shared<ConsoleState>())
    {
        if (input_system_)
        {
            key_listener_handle_ = input_system_->AddKeyListener(
                [this](const KeyEvent &event) { HandleKeyEvent(event); });
        }
    }

    EditorConsoleComponent::~EditorConsoleComponent()
    {
        if (input_system_ && key_listener_handle_ != 0)
        {
            input_system_->RemoveKeyListener(key_listener_handle_);
        }
        state_.reset();
    }

    void EditorConsoleComponent::HandleKeyEvent(const KeyEvent &event)
    {
        if (event.key == GLFW_KEY_GRAVE_ACCENT && event.action == GLFW_PRESS)
        {
            is_open_ = !is_open_;
            completion_candidates_.clear();
            if (is_open_)
            {
                focus_input_ = true;
            }
        }
    }

    void EditorConsoleComponent::Render()
    {
        DrainCompletions();
        if (!is_open_)
        {
            return;
        }

        ImGui::SetNextWindowPos(ImVec2(24.0f, 48.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(720.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Command Console", &is_open_))
        {
            if (code_font_)
            {
                ImGui::PushFont(code_font_);
            }
            if (ImGui::BeginChild("##command_output", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()),
                                  true))
            {
                for (const std::string &line : output_)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndChild();
            }

            if (focus_input_)
            {
                ImGui::SetKeyboardFocusHere();
                focus_input_ = false;
            }
            const ImGuiInputTextFlags flags =
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CallbackCompletion |
                ImGuiInputTextFlags_CallbackHistory |
                ImGuiInputTextFlags_CallbackEdit;
            if (ImGui::InputText("##command_input", input_buffer_.data(), input_buffer_.size(),
                                 flags, &EditorConsoleComponent::InputCallback, this))
            {
                Submit();
            }

            if (!completion_candidates_.empty())
            {
                ImGui::BeginChild("##command_completion", ImVec2(0.0f, 72.0f), true);
                for (const std::string &candidate : completion_candidates_)
                {
                    if (ImGui::Selectable(candidate.c_str()))
                    {
                        ReplaceCurrentToken(candidate);
                        completion_candidates_.clear();
                        focus_input_ = true;
                        break;
                    }
                }
                ImGui::EndChild();
            }
            if (code_font_)
            {
                ImGui::PopFont();
            }
        }
        ImGui::End();
    }

    int EditorConsoleComponent::InputCallback(ImGuiInputTextCallbackData *data)
    {
        auto *const console = static_cast<EditorConsoleComponent *>(data->UserData);
        if (!console)
        {
            return 0;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            console->completion_candidates_ = runtime::command::CommandParser::Complete(
                std::string_view(data->Buf, static_cast<size_t>(data->BufTextLen)),
                console->registry_ ? console->registry_->List()
                                   : std::vector<runtime::command::CommandDesc>{});
            if (console->completion_candidates_.size() == 1)
            {
                console->ReplaceCurrentToken(console->completion_candidates_.front());
                console->completion_candidates_.clear();
            }
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            if (console->history_.empty())
            {
                return 0;
            }

            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (console->history_cursor_ < 0)
                {
                    console->history_cursor_ = static_cast<int>(console->history_.size()) - 1;
                }
                else if (console->history_cursor_ > 0)
                {
                    --console->history_cursor_;
                }
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (console->history_cursor_ >= 0)
                {
                    ++console->history_cursor_;
                    if (console->history_cursor_ >= static_cast<int>(console->history_.size()))
                    {
                        console->history_cursor_ = -1;
                    }
                }
            }

            SetBuffer(data, console->history_cursor_ < 0
                                 ? std::string{}
                                 : console->history_[static_cast<size_t>(console->history_cursor_)]);
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
        {
            console->completion_candidates_.clear();
        }
        return 0;
    }

    void EditorConsoleComponent::Submit()
    {
        const std::string text(input_buffer_.data());
        input_buffer_.fill('\0');
        history_cursor_ = -1;
        completion_candidates_.clear();
        if (text.empty())
        {
            return;
        }

        const auto existing = std::find(history_.begin(), history_.end(), text);
        if (existing != history_.end())
        {
            history_.erase(existing);
        }
        history_.push_back(text);
        while (history_.size() > kMaxHistoryEntries)
        {
            history_.pop_front();
        }
        AppendOutput("> " + text);

        if (!registry_)
        {
            AppendOutput("[failed] command registry unavailable");
            return;
        }

        const std::weak_ptr<ConsoleState> weak_state = state_;
        const runtime::command::CommandResult result = registry_->ExecuteText(
            text,
            {runtime::command::CommandOrigin::UserConsole,
             runtime::command::CommandThread::Immediate},
            [weak_state](const runtime::command::CommandResult &completed)
            {
                if (const auto state = weak_state.lock())
                {
                    std::scoped_lock lock(state->mutex);
                    state->completed_results.push_back(completed);
                }
            });
        AppendResult(result);
    }

    void EditorConsoleComponent::ReplaceCurrentToken(const std::string &candidate)
    {
        std::string text(input_buffer_.data());
        const size_t separator = text.find_last_of(" \t");
        const size_t token_start = separator == std::string::npos ? 0 : separator + 1;
        text.replace(token_start, std::string::npos, candidate);
        const size_t length = std::min(input_buffer_.size() - 1, text.size());
        std::memcpy(input_buffer_.data(), text.data(), length);
        input_buffer_[length] = '\0';
    }

    void EditorConsoleComponent::DrainCompletions()
    {
        if (!state_)
        {
            return;
        }
        std::deque<runtime::command::CommandResult> results;
        {
            std::scoped_lock lock(state_->mutex);
            results.swap(state_->completed_results);
        }
        for (const auto &result : results)
        {
            AppendResult(result);
        }
    }

    void EditorConsoleComponent::AppendResult(const runtime::command::CommandResult &result)
    {
        std::string line = "[" + std::string(StatusName(result.status)) + "]";
        if (!result.message.empty())
        {
            line += " " + result.message;
        }
        if (result.request_id != 0)
        {
            line += " (#" + std::to_string(result.request_id) + ")";
        }
        if (const std::string *const output_path = FindString(result.data, "output_path");
            output_path && !output_path->empty())
        {
            line += " -> " + *output_path;
        }
        AppendOutput(std::move(line));
    }

    void EditorConsoleComponent::AppendOutput(std::string text)
    {
        output_.push_back(std::move(text));
        while (output_.size() > kMaxOutputEntries)
        {
            output_.pop_front();
        }
    }
}
