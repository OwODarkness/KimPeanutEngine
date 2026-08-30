#ifndef KPENGINE_RUNTIME_INPUT_SYSTEM_H
#define KPENGINE_RUNTIME_INPUT_SYSTEM_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

#include "delegate/event_dispatcher.h"
#include "base/base.h"
#include "input_action.h"
#include "gamepad_input.h"

struct GLFWwindow;
namespace kpengine::input{
    class InputContext;

    class InputSystem{
    public:
        void Initialize();
        void Shutdown();
        void AddContext(const std::string& name, std::shared_ptr<InputContext> context);
        void SetActiveContext(const std::string& name);
        // Enables/disables processing for the active gameplay context without
        // affecting editor key listeners. The flag is safe to change while
        // window callbacks are being delivered.
        void SetActiveContextEnabled(bool enabled);
        std::shared_ptr<InputContext> GetInputContext(const std::string& name);

        using KeyListenerHandle = uint64_t;
        KeyListenerHandle AddKeyListener(std::function<void(const KeyEvent &)> listener);
        void RemoveKeyListener(KeyListenerHandle handle);

        void BindMouseButtonEvent(EventDispatcher<MouseButtonEvent>& dispatcher);
        void BindKeyEvent(EventDispatcher<KeyEvent>& dispatcher);
        void BindCursorEvent(EventDispatcher<CursorEvent>& dispatcher);
        void BindScrollEvent(EventDispatcher<ScrollEvent>& dispatcher);
        void BindGamepadEvent(EventDispatcher<GamepadStateEvent>& dispatcher);

        // Action callbacks can run on the render thread while Gameplay ticks
        // on the game thread. Queue only copied logical values here; gameplay
        // consumes and clears the completed snapshot at its frame boundary.
        void EnqueueActionEvent(const std::string &action_name, const InputState &state,
                                InputActionSource source = InputActionSource::Unknown,
                                uint8_t component_mask = kInputActionAllComponents);
        InputFrameSnapshot ConsumeFrameSnapshot();
        // The viewport calls this when GLFW changes cursor mode so the first
        // relative event in the new mode is a baseline, not a jump.
        void ResetCursorTracking();

        void SetGamepadInputSettings(const GamepadInputSettings &settings)
        {
            gamepad_input_settings_ = settings;
        }
        const GamepadInputSettings &GetGamepadInputSettings() const
        {
            return gamepad_input_settings_;
        }
    private:
        std::unordered_map<std::string, std::shared_ptr<InputContext>> contexts_;
        std::string active_context_;
        std::atomic<bool> active_context_enabled_{true};

        double last_cursor_xpos_;
        double last_cursor_ypos_;
        bool is_first_cursor_;
    private:
        void MouseButtonExec(const MouseButtonEvent& event);
        void KeyExec(const KeyEvent& event);
        void CursorPosExec(const CursorEvent& event);
        void ScrollExec(const ScrollEvent& event);
        void GamepadExec(const GamepadStateEvent &event);

        std::unordered_map<KeyListenerHandle, std::function<void(const KeyEvent &)>> key_listeners_;
        KeyListenerHandle next_listener_handle_ = 1;
        bool initialized_ = false;

        std::mutex frame_snapshot_mutex_;
        std::vector<InputActionEvent> pending_frame_events_;

        GamepadInputSettings gamepad_input_settings_{};
        int active_gamepad_index_ = -1;
        std::array<uint8_t, kGamepadButtonCount> gamepad_button_states_{};

    };
}

#endif
