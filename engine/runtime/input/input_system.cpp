#include "input_system.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include "input_context.h"
#include "log/logger.h"
namespace kpengine::input
{

    void InputSystem::Initialize()
    {
        std::scoped_lock lock(frame_snapshot_mutex_);
        pending_frame_events_.clear();
        active_context_.clear();
        active_context_enabled_.store(true, std::memory_order_release);
        last_cursor_xpos_ = 0.0;
        last_cursor_ypos_ = 0.0;
        is_first_cursor_ = true;
        active_gamepad_index_ = -1;
        gamepad_button_states_.fill(0);
        initialized_ = true;
    }

    void InputSystem::Shutdown()
    {
        std::scoped_lock lock(frame_snapshot_mutex_);
        pending_frame_events_.clear();
        contexts_.clear();
        active_context_.clear();
        active_context_enabled_.store(true, std::memory_order_release);
        key_listeners_.clear();
        next_listener_handle_ = 1;
        initialized_ = false;
        is_first_cursor_ = true;
        last_cursor_xpos_ = 0.0;
        last_cursor_ypos_ = 0.0;
        active_gamepad_index_ = -1;
        gamepad_button_states_.fill(0);
    }

    void InputSystem::AddContext(const std::string &name, std::shared_ptr<InputContext> context)
    {
        if (context == nullptr)
        {
            return;
        }
        contexts_[name] = context;
    }
    void InputSystem::SetActiveContext(const std::string &name)
    {
        active_context_ = name;
    }

    void InputSystem::SetActiveContextEnabled(bool enabled)
    {
        std::scoped_lock lock(frame_snapshot_mutex_);
        pending_frame_events_.clear();
        active_context_enabled_.store(enabled, std::memory_order_release);
    }

    std::shared_ptr<InputContext> InputSystem::GetInputContext(const std::string &name)
    {
        auto it = contexts_.find(name);
        if (it == contexts_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    InputSystem::KeyListenerHandle InputSystem::AddKeyListener(
        std::function<void(const KeyEvent &)> listener)
    {
        if (!listener)
        {
            return 0;
        }
        const KeyListenerHandle handle = next_listener_handle_++;
        key_listeners_.emplace(handle, std::move(listener));
        return handle;
    }

    void InputSystem::EnqueueActionEvent(const std::string &action_name,
                                         const InputState &state,
                                         InputActionSource source,
                                         uint8_t component_mask)
    {
        if (action_name.empty())
        {
            return;
        }
        std::scoped_lock lock(frame_snapshot_mutex_);
        pending_frame_events_.push_back({action_name, state, source, component_mask});
    }

    InputFrameSnapshot InputSystem::ConsumeFrameSnapshot()
    {
        InputFrameSnapshot snapshot;
        std::scoped_lock lock(frame_snapshot_mutex_);
        snapshot.events.swap(pending_frame_events_);
        return snapshot;
    }

    void InputSystem::ResetCursorTracking()
    {
        std::scoped_lock lock(frame_snapshot_mutex_);
        last_cursor_xpos_ = 0.0;
        last_cursor_ypos_ = 0.0;
        is_first_cursor_ = true;
    }

    void InputSystem::RemoveKeyListener(const KeyListenerHandle handle)
    {
        if (handle != 0)
        {
            key_listeners_.erase(handle);
        }
    }

        void InputSystem::BindMouseButtonEvent(EventDispatcher<MouseButtonEvent>& dispatcher)
        {
            dispatcher.Bind(std::bind(&InputSystem::MouseButtonExec, this, std::placeholders::_1));
        }
        void InputSystem::BindKeyEvent(EventDispatcher<KeyEvent>& dispatcher)
        {
            dispatcher.Bind(std::bind(&InputSystem::KeyExec, this, std::placeholders::_1));

        }
        void InputSystem::BindCursorEvent(EventDispatcher<CursorEvent>& dispatcher)
        {
            dispatcher.Bind(std::bind(&InputSystem::CursorPosExec, this, std::placeholders::_1));

        }
        void InputSystem::BindScrollEvent(EventDispatcher<ScrollEvent>& dispatcher)
        {
            dispatcher.Bind(std::bind(&InputSystem::ScrollExec, this, std::placeholders::_1));

        }
        void InputSystem::BindGamepadEvent(EventDispatcher<GamepadStateEvent>& dispatcher)
        {
            dispatcher.Bind(std::bind(&InputSystem::GamepadExec, this, std::placeholders::_1));
        }

    void InputSystem::MouseButtonExec(const MouseButtonEvent& event)
    {

        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        InputTriggleType triggle_type;
        switch (event.action)
        {
        case GLFW_PRESS:
            triggle_type = InputTriggleType::Pressed;
            break;
        case GLFW_RELEASE:
            triggle_type = InputTriggleType::Released;
            break;
        default:
            triggle_type = InputTriggleType::Pressed;
            break;
        }
        if (active_context_enabled_.load(std::memory_order_acquire))
        {
            it->second->ProcessKeyInput({InputDevice::Mouse, event.code}, triggle_type,
                                         event.mods);
        }
    }
    void InputSystem::KeyExec(const KeyEvent& event)
    {
        auto it = contexts_.find(active_context_);
        if (it != contexts_.end() && active_context_enabled_.load(std::memory_order_acquire))
        {
            InputTriggleType triggle_type;
            switch (event.action)
            {
            case GLFW_PRESS:
                triggle_type = InputTriggleType::Pressed;
                break;
            case GLFW_RELEASE:
                triggle_type = InputTriggleType::Released;
                break;
            case GLFW_REPEAT:
                triggle_type = InputTriggleType::Held;
                break;
            default:
                triggle_type = InputTriggleType::Pressed;
                break;
            }
            it->second->ProcessKeyInput({InputDevice::Keyboard, event.key}, triggle_type,
                                         event.mods);
        }

        std::vector<std::function<void(const KeyEvent &)>> listeners;
        listeners.reserve(key_listeners_.size());
        for (const auto &listener : key_listeners_)
        {
            listeners.push_back(listener.second);
        }
        for (const auto &listener : listeners)
        {
            listener(event);
        }
    }
    void InputSystem::CursorPosExec(const CursorEvent& event)
    {
        if (!active_context_enabled_.load(std::memory_order_acquire))
        {
            is_first_cursor_ = true;
            return;
        }
        if (is_first_cursor_ == true)
        {
            is_first_cursor_ = false;
            last_cursor_xpos_ = event.xpos;
            last_cursor_ypos_ = event.ypos;
            return;
        }

        float delta_x = (float)(event.xpos - last_cursor_xpos_);
        float delta_y = (float)(event.ypos - last_cursor_ypos_);
        last_cursor_xpos_ = event.xpos;
        last_cursor_ypos_ = event.ypos;

        auto it = contexts_.find(active_context_);

        if (it == contexts_.end() ||
            !active_context_enabled_.load(std::memory_order_acquire))
        {
            return;
        }
        it->second->ProcessAxis2DInput({InputDevice::Mouse, KPENGINE_MOUSE_CURSOR}, delta_x, delta_y);
        
    }

    void InputSystem::ScrollExec(const ScrollEvent& event)
    {
        auto it = contexts_.find(active_context_);
        if (it == contexts_.end())
        {
            return;
        }
        it->second->ProcessAxis1DInput({InputDevice::Mouse, KPENGINE_MOUSE_SCROLL}, static_cast<float>(event.yoffset));
    }

    void InputSystem::GamepadExec(const GamepadStateEvent &event)
    {
        auto it = contexts_.find(active_context_);
        if (it == contexts_.end() || event.gamepad_index < 0 ||
            !active_context_enabled_.load(std::memory_order_acquire))
        {
            return;
        }

        InputContext &context = *it->second;
        if (!event.connected)
        {
            if (active_gamepad_index_ != event.gamepad_index)
            {
                return;
            }
            for (std::size_t index = 0; index < gamepad_button_states_.size(); ++index)
            {
                if (gamepad_button_states_[index] != 0)
                {
                    context.ProcessKeyInput(
                        {InputDevice::Gamepad,
                         static_cast<int>(GamepadButtonCode::A) + static_cast<int>(index)},
                        InputTriggleType::Released, 0);
                }
            }
            gamepad_button_states_.fill(0);
            context.ProcessAxis2DInput(
                {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::LeftStick)}, 0.0f,
                0.0f);
            context.ProcessAxis2DInput(
                {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::RightStick)}, 0.0f,
                0.0f);
            context.ProcessAxis1DInput(
                {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::LeftTrigger)}, 0.0f);
            context.ProcessAxis1DInput(
                {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::RightTrigger)}, 0.0f);
            active_gamepad_index_ = -1;
            return;
        }

        if (active_gamepad_index_ < 0)
        {
            active_gamepad_index_ = event.gamepad_index;
            gamepad_button_states_.fill(0);
        }
        if (active_gamepad_index_ != event.gamepad_index)
        {
            return;
        }

        const Vector2f left_stick = ApplyGamepadStickSettings(
            {event.axes[0], event.axes[1]}, gamepad_input_settings_.left_stick);
        const Vector2f right_stick = ApplyGamepadStickSettings(
            {event.axes[2], event.axes[3]}, gamepad_input_settings_.right_stick);
        context.ProcessAxis2DInput(
            {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::LeftStick)}, left_stick.x_,
            left_stick.y_);
        context.ProcessAxis2DInput(
            {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::RightStick)}, right_stick.x_,
            right_stick.y_);
        context.ProcessAxis1DInput(
            {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::LeftTrigger)}, event.axes[4]);
        context.ProcessAxis1DInput(
            {InputDevice::Gamepad, static_cast<int>(GamepadAxisCode::RightTrigger)}, event.axes[5]);

        for (std::size_t index = 0; index < gamepad_button_states_.size(); ++index)
        {
            const uint8_t pressed = event.buttons[index] != 0 ? 1 : 0;
            if (pressed != gamepad_button_states_[index])
            {
                context.ProcessKeyInput(
                    {InputDevice::Gamepad,
                     static_cast<int>(GamepadButtonCode::A) + static_cast<int>(index)},
                    pressed != 0 ? InputTriggleType::Pressed : InputTriggleType::Released, 0);
                gamepad_button_states_[index] = pressed;
            }
        }
    }


}
