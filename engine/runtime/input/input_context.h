#ifndef KPENGINE_RUNTIME_INPUT_CONTEXT_H
#define KPENGINE_RUNTIME_INPUT_CONTEXT_H

#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>
#include "input_action.h"
#include "input_key.h"

namespace kpengine::input{

struct InputHandle {

    InputHandle():id(0){}
    InputHandle(uint32_t in_id):id(in_id){}
    uint32_t id;
    bool operator==(const InputHandle& other) const { return id == other.id; }
    bool operator!=(const InputHandle& other) const { return id != other.id; }

    bool IsValid() const
    {
        return id != 0;
    }
};

    struct InputHandleHash {
        std::size_t operator()(const InputHandle& handle) const {
            return std::hash<uint32_t>()(handle.id);
        }
    };

class InputContext{
public:
    InputHandle Bind(std::shared_ptr<InputAction> action, const InputKey& key);
    void UnBind(InputHandle input_handle);

    std::string GetNameByHandle(InputHandle input_handle) const;
    InputHandle GetHandleByName(const std::string& name) const;
    InputHandle GetHandleByInputKey(InputKey key) const;
    std::shared_ptr<InputAction> GetInputActionByHandle(InputHandle input_handle) const;
    std::shared_ptr<InputAction> GetInputActionByInputKey(InputKey key) const;
public:
    void ProcessKeyInput(InputKey key, InputTriggleType triggle_type, int mods);
    void ProcessAxis2DInput(InputKey key, float deltax, float deltay);
private:
    std::unordered_map<InputHandle, std::shared_ptr<InputAction>, InputHandleHash> input_actions;
    std::unordered_map<std::string, InputHandle> name_to_handle_;
    std::unordered_map<InputKey, InputHandle, InputKeyHasher> input_bindings_;
    std::unordered_map<InputHandle, InputKey, InputHandleHash> handle_to_key;
    uint32_t next_handle_id_ = 1;
 };

}


#endif