#include "input_context.h"
#include <iostream>

#include "runtime/core/log/logger.h"

namespace kpengine::input
{
    InputHandle InputContext::Bind(std::shared_ptr<InputAction> action, const InputKey &key)
    {
        if (action == nullptr)
        {
            return InputHandle{0};
        }

        InputHandle input_handle{next_handle_id_++};
        input_actions[input_handle] = action;
        name_to_handle_[action->name_] = input_handle;
        input_bindings_[key] = input_handle;
        handle_to_key[input_handle] = key;

        return input_handle;
    }

    void InputContext::UnBind(InputHandle input_handle)
    {
        auto it = handle_to_key.find(input_handle);
        if (it == handle_to_key.end())
        {
            return;
        }
        input_bindings_.erase(it->second);
    }

    std::string InputContext::GetNameByHandle(InputHandle input_handle) const
    {
        auto it = input_actions.find(input_handle);
        if (it == input_actions.end())
        {
            return "";
        }
        else
        {
            if (it->second == nullptr)
            {
                KP_LOG("InputLog", LOG_LEVEL_ERROR, "met nullptr InputAction Object when calling GetNameFromHandle");
                return "";
            }
            return it->second->name_;
        }
    }

    InputHandle InputContext::GetHandleByName(const std::string &name) const
    {
        auto it = name_to_handle_.find(name);
        if (it == name_to_handle_.end())
        {
            return InputHandle();
        }
        return it->second;
    }

    InputHandle InputContext::GetHandleByInputKey(InputKey key) const
    {
        auto it = input_bindings_.find(key);
        if (it == input_bindings_.end())
        {
            return InputHandle();
        }
        return it->second;
    }

    std::shared_ptr<InputAction> InputContext::GetInputActionByHandle(InputHandle input_handle) const
    {
        auto it = input_actions.find(input_handle);
        if (it == input_actions.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<InputAction> InputContext::GetInputActionByInputKey(InputKey key) const
    {
        InputHandle input_handle = GetHandleByInputKey(key);
        if (!input_handle.IsValid())
        {
            return nullptr;
        }
        return GetInputActionByHandle(input_handle);
    }

    void InputContext::ProcessKeyInput(InputKey key, InputTriggleType triggle_type, int mods)
    {
        std::shared_ptr<InputAction> input_action = GetInputActionByInputKey(key);
        if (input_action == nullptr)
        {
            return;
        }
        // TODO: Modifier?
        InputState state{.triggle_type = triggle_type, .value = input_action->default_value};
        input_action->callback_(state);
    }

    void InputContext::ProcessAxis2DInput(InputKey key, float deltax, float deltay)
    {
        std::shared_ptr<InputAction> input_action = GetInputActionByInputKey(key);
        if (input_action == nullptr)
        {
            return;
        }
        InputState state{.value = Vector2f(deltax, deltay)};
        input_action->callback_(state);
    }
}