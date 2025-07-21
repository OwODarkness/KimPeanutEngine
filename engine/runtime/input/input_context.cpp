#include "input_context.h"
#include "runtime/core/log/logger.h"

namespace kpengine::input{

    InputHandle InputContext::CreatAction(const std::string& name, InputValueType value_type)
    {
        if(name == "")
        {
            return InputHandle();
        }
        std::shared_ptr<InputAction> input_action = std::make_shared<InputAction>();
        input_action->name_ = name;
        input_action->value_type_ = value_type;
        InputHandle handle{next_handle_id_};

        input_actions[handle] = input_action;
        name_to_handle_[input_action->name_] = handle;
        return handle;
    }
    

    std::string InputContext::GetNameFromHandle(InputHandle handle) const
    {
        auto it = input_actions.find(handle);
        if(it == input_actions.end())
        {
            return "";
        }
        else
        {
            if(it->second == nullptr)
            {
                KP_LOG("InputLog", LOG_LEVEL_ERROR, "met nullptr InputAction Object when calling GetNameFromHandle");
                return "";
            }
            return it->second->name_;
        }
    }

    InputHandle InputContext::GetHandleFromName(const std::string & name) const
    {
        auto it = name_to_handle_.find(name);
        if(it == name_to_handle_.end())
        {
            return InputHandle();
        }
        return it->second;
    }

    std::shared_ptr<InputAction> InputContext::GetInputActionFromHandle(InputHandle handle) const
    {
        auto it = input_actions.find(handle);
        if(it == input_actions.end())
        {
            return nullptr;
        }
        return it->second;
    }
    
}