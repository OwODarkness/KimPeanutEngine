#ifndef KPENGINE_SCRIPT_LUA_VM_H
#define KPENGINE_SCRIPT_LUA_VM_H

#include <string>
#include <filesystem>
#include <optional>
#include <sol/sol.hpp>

namespace kpengine::script::lua
{
    class LuaVM
    {
    public:
        LuaVM();
        ~LuaVM();

        bool Initialize();
        void Shutdown();

        bool ExecuteString(const std::string &code);
        bool ExecuteFile(const std::filesystem::path &path);
        template <typename Func>
        void RegisterFunction(const std::string &name, Func &&func)
        {
            if (initialized_ == false)
                return;
            lua_.set_function(name, std::forward<Func>(func));
        }

        template <typename T>
        void SetGlobal(const std::string &name, T &&value)
        {
            if (initialized_ == false)
                return;
            lua_[name] = std::forward<T>(value);
        }

    template <typename T>
    std::optional<T> GetGlobal(const std::string &name) {
        if (!initialized_) {
            return std::nullopt;
        }
        
        auto result = lua_[name].get<T>();
        if (result.valid()) {
            return result;
        }
        return std::nullopt;
    }

    template<typename... Args>
    auto CallFunction(const std::string& funcName, Args&&... args) {
        if (!initialized_) {
            throw std::runtime_error("Lua not initialized");
        }
        
        sol::function func = lua_[funcName];
        if (!func.valid()) {
            throw std::runtime_error("Function not found: " + funcName);
        }
        
        return func(std::forward<Args>(args)...);
    }

    private:
        sol::state lua_;
        bool initialized_ = false;
    };
}

#endif