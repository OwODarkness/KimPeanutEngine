#ifndef KPENGINE_RUNTIME_SCRIPT_COMMAND_LUA_COMMAND_BRIDGE_H
#define KPENGINE_RUNTIME_SCRIPT_COMMAND_LUA_COMMAND_BRIDGE_H

#include <string>
#include <thread>

#include "command/command_types.h"

namespace kpengine::runtime::command
{
    class CommandRegistry;
}

namespace kpengine::script::lua
{
    class LuaVM;
}

namespace kpengine::runtime::script
{
    // Engine-facing Lua adapter. RuntimeCommand stays independent of sol2; this
    // class is the only layer that translates Lua values into CommandCall data.
    class LuaCommandBridge final
    {
    public:
        LuaCommandBridge(command::CommandRegistry& registry, ::kpengine::script::lua::LuaVM& lua_vm,
                         command::CommandCapability capabilities = command::CommandCapability::None);
        ~LuaCommandBridge();

        LuaCommandBridge(const LuaCommandBridge&) = delete;
        LuaCommandBridge& operator=(const LuaCommandBridge&) = delete;

        bool Initialize();
        void Shutdown();
        bool IsInitialized() const noexcept { return initialized_; }

    private:
        command::CommandRegistry& registry_;
        ::kpengine::script::lua::LuaVM& lua_vm_;
        command::CommandCapability capabilities_;
        std::thread::id lua_thread_id_{};
        bool initialized_ = false;
    };
}

#endif
