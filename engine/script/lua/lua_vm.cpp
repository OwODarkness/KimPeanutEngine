#include "lua_vm.h"
#include "log/logger.h"
namespace kpengine::script::lua
{

    LuaVM::LuaVM() = default;

    LuaVM::~LuaVM()
    {
        Shutdown();
    }

    bool LuaVM::Initialize()
    {
        if (initialized_ == true)
        {
            KP_LOG("LuaVMLog", LOG_LEVEL_WARNING, "LuaVM has been Initialized");
            return false;
        }
        lua_.open_libraries(
            sol::lib::base,
            sol::lib::math,
            sol::lib::string,
            sol::lib::table,
            sol::lib::package
        );

        lua_["engine_name"] = "KPEngine";

        // lua_.set_function("print_cpp", [](const std::string& msg)
        // {
        //     std::cout << "[Lua] " << msg << std::endl;
        // });

        initialized_ = true;
        return false;
    }
    void LuaVM::Shutdown()
    {
        if(initialized_ == false)
            return ;
        initialized_ = false;
    }

    bool LuaVM::ExecuteString(const std::string &code)
    {
        sol::protected_function_result result = lua_.script(code);

        if (!result.valid())
        {
            sol::error err = result;
            const char* msg = err.what();
            KP_LOG("LuaVMLog", LOG_LEVEL_ERROR, msg);
            return false;
        }

        return true;
    }
    bool LuaVM::ExecuteFile(const std::filesystem::path &path)
    {
        sol::protected_function_result result = lua_.script_file(path.string());

        if (!result.valid())
        {
            sol::error err = result;
            const char* msg = err.what();
            KP_LOG("LuaVMLog", LOG_LEVEL_ERROR, msg);
            return false;
        }

        return true;
    }
}