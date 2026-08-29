#include "input_system.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "input_context.h"
#include "log/logger.h"
namespace kpengine::input
{

    void InputSystem::Initialize()
    {
        active_context_.clear();
        last_cursor_xpos_ = 0.0;
        last_cursor_ypos_ = 0.0;
        is_first_cursor_ = true;
        initialized_ = true;
    }

    void InputSystem::Shutdown()
    {
        contexts_.clear();
        active_context_.clear();
        key_listeners_.clear();
        next_listener_handle_ = 1;
        initialized_ = false;
        is_first_cursor_ = true;
        last_cursor_xpos_ = 0.0;
        last_cursor_ypos_ = 0.0;
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
        it->second->ProcessKeyInput({InputDevice::Mouse, event.code}, triggle_type, event.mods);
    }
    void InputSystem::KeyExec(const KeyEvent& event)
    {
        auto it = contexts_.find(active_context_);
        if (it != contexts_.end())
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

        if (it == contexts_.end())
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


}
