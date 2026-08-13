#include "script/lua/lua_vm.h"

#include <lua/lua.h>
#include <lua/lauxlib.h>

#include "log/logger.h"

namespace kpengine::script::lua
{
    // Forward-declared in lua_vm.h. Carried in the state's extra space (one
    // pointer slot) so the hook can reach the per-execution budget without a
    // registry lookup every fire.
    struct InstructionBudget
    {
        std::uint64_t remaining_steps;
    };

    namespace
    {
        // The hook fires every kHookStep instructions; the budget counts hook
        // steps, so the abort lands within [limit, limit + kHookStep] instructions.
        constexpr int kHookStep = 1000;

        void InstructionHook(lua_State* L, lua_Debug* /*ar*/)
        {
            auto* budget = *static_cast<InstructionBudget**>(lua_getextraspace(L));
            if (budget->remaining_steps != 0 && --budget->remaining_steps == 0)
                luaL_error(L, "script aborted: instruction limit exceeded");
        }
    }

    LuaVM::LuaVM() = default;

    LuaVM::~LuaVM()
    {
        Shutdown();
    }

    bool LuaVM::Initialize(std::uint64_t instruction_limit)
    {
        if (lua_)
        {
            RecordError("LuaVM::Initialize: already initialized");
            return false;
        }

        lua_ = std::make_unique<sol::state>();
        lua_->open_libraries(
            sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::package);

        // Sandbox: package.loadlib/cpath load arbitrary native code; first-party
        // scripts only need require. os/io/debug/coroutine are never opened.
        sol::table package = (*lua_)["package"];
        package["loadlib"] = sol::nil;
        package["cpath"] = sol::nil;

        // Per-state instruction budget in extra space; reset before each
        // top-level execution in ResetInstructionBudget().
        instruction_budget_ = std::make_unique<InstructionBudget>(InstructionBudget{0});
        *static_cast<InstructionBudget**>(lua_getextraspace(lua_->lua_state())) = instruction_budget_.get();
        lua_sethook(lua_->lua_state(), InstructionHook, LUA_MASKCOUNT, kHookStep);
        SetInstructionLimit(instruction_limit);

        (*lua_)["engine_name"] = "KPEngine";

        return true;
    }

    void LuaVM::Shutdown()
    {
        instruction_budget_.reset();
        lua_.reset();
        last_error_.clear();
    }

    void LuaVM::SetInstructionLimit(std::uint64_t instructions)
    {
        instruction_steps_budget_ = (instructions == 0) ? 0 : (instructions / kHookStep + 1);
        if (instruction_budget_)
            instruction_budget_->remaining_steps = instruction_steps_budget_;
    }

    void LuaVM::ResetInstructionBudget()
    {
        if (instruction_budget_)
            instruction_budget_->remaining_steps = instruction_steps_budget_;
    }

    bool LuaVM::ExecuteString(const std::string& code)
    {
        if (!lua_)
        {
            RecordError("LuaVM::ExecuteString: not initialized");
            return false;
        }
        ResetInstructionBudget();
        // script_pass_on_error: return the error result instead of throwing, so
        // HandleResult can log it. (default handler throws.)
        sol::protected_function_result result = lua_->safe_script(code, sol::script_pass_on_error, "[LuaVM]");
        return HandleResult(result);
    }

    bool LuaVM::ExecuteFile(const std::filesystem::path& path)
    {
        if (!lua_)
        {
            RecordError("LuaVM::ExecuteFile: not initialized");
            return false;
        }
        ResetInstructionBudget();
        sol::protected_function_result result = lua_->safe_script_file(path.string(), sol::script_pass_on_error);
        return HandleResult(result);
    }

    bool LuaVM::HandleResult(sol::protected_function_result& result)
    {
        if (result.valid())
            return true;
        sol::error err = result.get<sol::error>();
        RecordError(err.what());
        return false;
    }

    void LuaVM::RecordError(const char* what)
    {
        last_error_ = (what != nullptr) ? what : "unknown Lua error";
        KP_LOG("LuaVMLog", LOG_LEVEL_ERROR, "%s", last_error_.c_str());
    }

    bool LuaVM::WarnNotInitialized(const char* operation)
    {
        KP_LOG("LuaVMLog", LOG_LEVEL_WARNING, "LuaVM::%s ignored: not initialized", operation);
        return false;
    }
}
