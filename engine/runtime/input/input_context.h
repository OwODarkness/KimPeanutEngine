#ifndef KPENGINE_RUNTIME_INPUT_CONTEXT_H
#define KPENGINE_RUNTIME_INPUT_CONTEXT_H

#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "input_action.h"
#include "input_binding.h"

namespace kpengine::input{

struct InputHandle {

    InputHandle():id(0){}
    InputHandle(uint32_t in_id):id(in_id){}
    uint32_t id;
    bool operator==(const InputHandle& other) const { return id == other.id; }
    bool operator!=(const InputHandle& other) const { return id != other.id; }

    static bool IsValid(InputHandle handle)
    {
        return handle.id != 0;
    }
};

    struct InputHandleHash {
        std::size_t operator()(const InputHandle& handle) const {
            return std::hash<uint32_t>()(handle.id);
        }
    };

class InputContext{
public:
    InputHandle CreatAction(const std::string& name, InputValueType value_type);
    std::string GetNameFromHandle(InputHandle handle) const;
    InputHandle GetHandleFromName(const std::string& name) const;
    std::shared_ptr<InputAction> GetInputActionFromHandle(InputHandle handle) const;
private:
    std::unordered_map<InputHandle, std::shared_ptr<InputAction>, InputHandleHash> input_actions;
    std::unordered_map<std::string, InputHandle> name_to_handle_;
    uint32_t next_handle_id_ = 1;
 };

}


#endif