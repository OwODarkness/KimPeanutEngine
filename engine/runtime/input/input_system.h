#ifndef KPENGINE_RUNTIME_INPUT_SYSTEM_H
#define KPENGINE_RUNTIME_INPUT_SYSTEM_H

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "delegate/event_dispatcher.h"
#include "base/base.h"

struct GLFWwindow;
namespace kpengine::input{
    class InputContext;

    class InputSystem{
    public:
        void Initialize();
        void Shutdown();
        void AddContext(const std::string& name, std::shared_ptr<InputContext> context);
        void SetActiveContext(const std::string& name);
        std::shared_ptr<InputContext> GetInputContext(const std::string& name);

        using KeyListenerHandle = uint64_t;
        KeyListenerHandle AddKeyListener(std::function<void(const KeyEvent &)> listener);
        void RemoveKeyListener(KeyListenerHandle handle);

        void BindMouseButtonEvent(EventDispatcher<MouseButtonEvent>& dispatcher);
        void BindKeyEvent(EventDispatcher<KeyEvent>& dispatcher);
        void BindCursorEvent(EventDispatcher<CursorEvent>& dispatcher);
        void BindScrollEvent(EventDispatcher<ScrollEvent>& dispatcher);
    private:
        std::unordered_map<std::string, std::shared_ptr<InputContext>> contexts_;
        std::string active_context_;

        double last_cursor_xpos_;
        double last_cursor_ypos_;
        bool is_first_cursor_;
    private:
        void MouseButtonExec(const MouseButtonEvent& event);
        void KeyExec(const KeyEvent& event);
        void CursorPosExec(const CursorEvent& event);
        void ScrollExec(const ScrollEvent& event);

        std::unordered_map<KeyListenerHandle, std::function<void(const KeyEvent &)>> key_listeners_;
        KeyListenerHandle next_listener_handle_ = 1;
        bool initialized_ = false;

    };
}

#endif
