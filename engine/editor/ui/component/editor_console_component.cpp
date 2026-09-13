#include "editor/ui/component/editor_console_component.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
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
            data->BufDirty = true;
        }

        std::string ReplaceCurrentTokenText(std::string_view text,
                                             const std::string &candidate)
        {
            std::string result(text);
            const size_t separator = result.find_last_of(" 	");
            const size_t token_start = separator == std::string::npos ? 0 : separator + 1;
            result.replace(token_start, std::string::npos, candidate);
            return result;
        }

        bool IsCommandNameInput(std::string_view text)
        {
            return text.find_first_of(" \t\r\n") == std::string_view::npos;
        }

        std::string FindInlineCommandSuggestion(runtime::command::CommandRegistry *registry,
                                                 std::string_view input)
        {
            if (registry == nullptr || !IsCommandNameInput(input))
            {
                return {};
            }

            for (const std::string &candidate : registry->CompleteCommandNames(input))
            {
                if (candidate.size() > input.size() &&
                    candidate.compare(0, input.size(), input) == 0)
                {
                    return candidate.substr(input.size());
                }
            }
            return {};
        }

        void DrawInlineCommandSuggestion(std::string_view typed, std::string_view suffix)
        {
            if (suffix.empty())
            {
                return;
            }

            const ImGuiStyle &style = ImGui::GetStyle();
            ImVec4 color = style.Colors[ImGuiCol_Text];
            color.x *= 0.55f;
            color.y *= 0.55f;
            color.z *= 0.55f;
            color.w *= 0.70f;

            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            const float text_x = item_min.x + style.FramePadding.x +
                                 ImGui::CalcTextSize(std::string(typed).c_str()).x;
            const float text_y = item_min.y + style.FramePadding.y;
            // Draw after InputText on the foreground list so the ghost cannot be
            // covered by the input widget's own draw commands.
            ImDrawList *const draw_list = ImGui::GetForegroundDrawList();
            draw_list->PushClipRect(item_min, item_max, true);
            draw_list->AddText(ImVec2(text_x, text_y),
                               ImGui::ColorConvertFloat4ToU32(color), suffix.data(),
                               suffix.data() + suffix.size());
            draw_list->PopClipRect();
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
        : EditorWindowComponent("Console", EditorWindowConfig{}), registry_(registry),
          input_system_(input_system), code_font_(code_font),
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
            // The tab strip and the View menu read this same value, so the hotkey
            // cannot get out of sync with either of them.
            if (visibility_ != nullptr)
            {
                visibility_->Toggle();
            }
            completion_candidates_.clear();
            if (IsVisible())
            {
                focus_input_ = true;
            }
        }
    }

    void EditorConsoleComponent::Pump()
    {
        // Deferred results arrive on the command thread; drain them whether or not
        // the console is on screen this frame.
        DrainCompletions();
    }

    void EditorConsoleComponent::RenderContent()
    {
        DrainCompletions();

        if (code_font_)
        {
            ImGui::PushFont(code_font_);
        }

        // EndChild must be called for every BeginChild regardless of its return value:
        // ImGui pushes the child onto a window stack either way, so guarding it with this
        // call's result skips EndChild whenever the child is clipped, which asserts.
        //
        // The opposite holds for EndTable and EndPopup, which MUST be guarded by their
        // Begin's return value. The two conventions look like an inconsistency and are not.
        ImGui::BeginChild("##command_output", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()),
                          true);
        for (const std::string &line : output_)
        {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();

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

        const std::string current_input(input_buffer_.data());
        const std::string inline_suggestion =
            FindInlineCommandSuggestion(registry_, current_input);
        DrawInlineCommandSuggestion(current_input, inline_suggestion);

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

    int EditorConsoleComponent::InputCallback(ImGuiInputTextCallbackData *data)
    {
        auto *const console = static_cast<EditorConsoleComponent *>(data->UserData);
        if (!console)
        {
            return 0;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            const std::string_view input(data->Buf, static_cast<size_t>(data->BufTextLen));
            if (console->registry_ && IsCommandNameInput(input))
            {
                console->completion_candidates_ = console->registry_->CompleteCommandNames(input);
            }
            else
            {
                console->completion_candidates_ = runtime::command::CommandParser::Complete(
                    input,
                    console->registry_ ? console->registry_->List()
                                       : std::vector<runtime::command::CommandDesc>{});
            }
            if (console->completion_candidates_.size() == 1)
            {
                const std::string completed = ReplaceCurrentTokenText(
                    std::string_view(data->Buf, static_cast<size_t>(data->BufTextLen)),
                    console->completion_candidates_.front());
                SetBuffer(data, completed);
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
