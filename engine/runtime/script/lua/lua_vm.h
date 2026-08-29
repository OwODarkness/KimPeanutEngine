#ifndef KPENGINE_RUNTIME_SCRIPT_LUA_LUA_VM_H
#define KPENGINE_RUNTIME_SCRIPT_LUA_LUA_VM_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <sol/sol.hpp>

namespace kpengine::script::lua
{
    // Generic Lua hosting layer — owns one sol2 state, no engine knowledge.
    // Engine bindings live above this (engine/runtime/script), never here.
    // Not thread-safe: one LuaVM per game thread.
    struct InstructionBudget;

    class LuaVM
    {
    public:
        static constexpr std::uint64_t kDefaultInstructionLimit = 10'000'000;

        LuaVM();
        ~LuaVM();

        LuaVM(const LuaVM&) = delete;
        LuaVM& operator=(const LuaVM&) = delete;
        LuaVM(LuaVM&&) = delete;
        LuaVM& operator=(LuaVM&&) = delete;

        // Creates and configures the state. `instruction_limit` caps how many
        // instructions one script execution may run before it is aborted
        // (0 = unlimited). Returns false (and logs) if already initialized.
        bool Initialize(std::uint64_t instruction_limit = kDefaultInstructionLimit);
        // Closes the state and frees everything it owns. Re-Initialize is valid.
        void Shutdown();
        bool IsInitialized() const { return lua_ != nullptr; }

        // Execute script text / file. Lua errors are logged, stored in LastError()
        // and returned as false (no exception).
        bool ExecuteString(const std::string& code);
        bool ExecuteFile(const std::filesystem::path& path);

        // Bind a C++ function / value into the global scope. Returns false and
        // warns if the VM isn't initialized.
        template <typename Func>
        bool RegisterFunction(const std::string& name, Func&& func);
        // Bind a function below a named global table without exposing the
        // underlying sol::state to engine-specific binding layers.
        template <typename Func>
        bool RegisterTableFunction(const std::string& table_name,
                                   const std::string& function_name, Func&& func);
        bool RemoveTableFunction(const std::string& table_name,
                                 const std::string& function_name);
        template <typename Func>
        bool RegisterNestedTableFunction(const std::string& table_name,
                                         const std::string& nested_table_name,
                                         const std::string& function_name, Func&& func);
        bool RemoveNestedTableFunction(const std::string& table_name,
                                       const std::string& nested_table_name,
                                       const std::string& function_name);
        template <typename T>
        bool SetGlobal(const std::string& name, T&& value);

        // Read a global. nullopt if the VM isn't initialized, the global is nil,
        // or its type doesn't match T.
        template <typename T>
        std::optional<T> GetGlobal(const std::string& name);

        // Call a global Lua function. nullopt if the VM isn't initialized or the
        // function is missing; otherwise a result to validate with valid() —
        // get<sol::error>() carries the Lua error text when invalid. Failed calls
        // are also recorded in LastError().
        template <typename... Args>
        std::optional<sol::protected_function_result> CallFunction(const std::string& name, Args&&... args);

        // Per-execution instruction budget (0 = unlimited), applied on the next
        // Execute*/CallFunction.
        void SetInstructionLimit(std::uint64_t instructions);

        // Most recent error from a failed operation ("" if none).
        const std::string& LastError() const { return last_error_; }

    private:
        bool WarnNotInitialized(const char* operation);
        void RecordError(const char* what);
        bool HandleResult(sol::protected_function_result& result);
        void ResetInstructionBudget();

        std::unique_ptr<sol::state> lua_;
        std::unique_ptr<InstructionBudget> instruction_budget_;
        std::uint64_t instruction_steps_budget_ = 0; // quota in hook steps, 0 = unlimited
        std::string last_error_;
    };

    template <typename Func>
    bool LuaVM::RegisterFunction(const std::string& name, Func&& func)
    {
        if (!lua_)
            return WarnNotInitialized("RegisterFunction");
        lua_->set_function(name, std::forward<Func>(func));
        return true;
    }

    template <typename Func>
    bool LuaVM::RegisterTableFunction(const std::string& table_name,
                                      const std::string& function_name, Func&& func)
    {
        if (!lua_)
            return WarnNotInitialized("RegisterTableFunction");
        sol::object existing = (*lua_)[table_name];
        if (existing.valid() && existing.get_type() != sol::type::nil &&
            existing.get_type() != sol::type::table)
        {
            RecordError(("LuaVM::RegisterTableFunction: global is not a table: " + table_name).c_str());
            return false;
        }
        sol::table table = existing.valid() && existing.get_type() == sol::type::table
                               ? existing.as<sol::table>()
                               : lua_->create_named_table(table_name);
        table.set_function(function_name, std::forward<Func>(func));
        return true;
    }

    template <typename Func>
    bool LuaVM::RegisterNestedTableFunction(const std::string& table_name,
                                            const std::string& nested_table_name,
                                            const std::string& function_name, Func&& func)
    {
        if (!lua_)
            return WarnNotInitialized("RegisterNestedTableFunction");
        sol::object root_existing = (*lua_)[table_name];
        if (root_existing.valid() && root_existing.get_type() != sol::type::nil &&
            root_existing.get_type() != sol::type::table)
        {
            RecordError(("LuaVM::RegisterNestedTableFunction: global is not a table: " + table_name).c_str());
            return false;
        }
        sol::table root = root_existing.valid() && root_existing.get_type() == sol::type::table
                              ? root_existing.as<sol::table>()
                              : lua_->create_named_table(table_name);
        sol::object nested_existing = root[nested_table_name];
        if (nested_existing.valid() && nested_existing.get_type() != sol::type::nil &&
            nested_existing.get_type() != sol::type::table)
        {
            RecordError(("LuaVM::RegisterNestedTableFunction: field is not a table: " + nested_table_name).c_str());
            return false;
        }
        sol::table nested = nested_existing.valid() && nested_existing.get_type() == sol::type::table
                                ? nested_existing.as<sol::table>()
                                : lua_->create_table();
        nested.set_function(function_name, std::forward<Func>(func));
        root[nested_table_name] = std::move(nested);
        return true;
    }

    template <typename T>
    bool LuaVM::SetGlobal(const std::string& name, T&& value)
    {
        if (!lua_)
            return WarnNotInitialized("SetGlobal");
        (*lua_)[name] = std::forward<T>(value);
        return true;
    }

    template <typename T>
    std::optional<T> LuaVM::GetGlobal(const std::string& name)
    {
        if (!lua_)
            return std::nullopt;
        auto value = (*lua_)[name];
        if (!value.valid() || !value.is<T>())
            return std::nullopt;
        return value.get<T>();
    }

    template <typename... Args>
    std::optional<sol::protected_function_result> LuaVM::CallFunction(const std::string& name, Args&&... args)
    {
        if (!lua_)
        {
            RecordError("LuaVM::CallFunction: not initialized");
            return std::nullopt;
        }
        ResetInstructionBudget();
        sol::protected_function fn = (*lua_)[name].get_or(sol::protected_function());
        if (!fn.valid())
        {
            RecordError(("LuaVM::CallFunction: function not found: " + name).c_str());
            return std::nullopt;
        }
        sol::protected_function_result result = fn(std::forward<Args>(args)...);
        if (!result.valid())
            HandleResult(result); // record the Lua error, still hand the result up
        return result;
    }
}

#endif
