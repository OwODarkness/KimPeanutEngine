#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "script/lua/lua_vm.h"

using namespace kpengine::script::lua;

namespace
{
    // Runs a setup fragment; the test aborts if it fails to parse/run.
    void ExecuteOk(LuaVM& vm, const std::string& code)
    {
        ASSERT_TRUE(vm.ExecuteString(code)) << vm.LastError();
    }
}

TEST(LuaVMTest, InitializeCreatesWorkingState)
{
    LuaVM vm;
    EXPECT_FALSE(vm.IsInitialized());
    ASSERT_TRUE(vm.Initialize());
    EXPECT_TRUE(vm.IsInitialized());

    EXPECT_TRUE(vm.ExecuteString("x = 2 + 3"));
    EXPECT_EQ(vm.GetGlobal<int>("x").value_or(-1), 5);
}

TEST(LuaVMTest, DoubleInitializeRejected)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_FALSE(vm.Initialize());

    // The second call must not clobber the first state.
    ExecuteOk(vm, "x = 42");
    EXPECT_EQ(vm.GetGlobal<int>("x").value_or(-1), 42);
}

TEST(LuaVMTest, ShutdownReleasesAndAllowsReinit)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    vm.Shutdown();
    EXPECT_FALSE(vm.IsInitialized());

    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm, "x = 1");
    EXPECT_EQ(vm.GetGlobal<int>("x").value_or(-1), 1);
}

TEST(LuaVMTest, EngineNameGlobalSet)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_EQ(vm.GetGlobal<std::string>("engine_name").value_or(""), "KPEngine");
}

TEST(LuaVMTest, ExecuteStringReportsSyntaxError)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_FALSE(vm.ExecuteString("local = "));
    EXPECT_FALSE(vm.LastError().empty());
}

TEST(LuaVMTest, ExecuteFile)
{
    std::filesystem::path script = std::filesystem::temp_directory_path() / "kpengine_lua_vm_test.lua";
    {
        std::ofstream out(script);
        out << "from_file = 7\n";
    }

    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_TRUE(vm.ExecuteFile(script));
    EXPECT_EQ(vm.GetGlobal<int>("from_file").value_or(-1), 7);

    std::filesystem::remove(script);
}

TEST(LuaVMTest, GetGlobalMissingReturnsNullopt)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_FALSE(vm.GetGlobal<int>("does_not_exist").has_value());
}

TEST(LuaVMTest, GetGlobalTypeMismatchReturnsNullopt)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm, "s = 'not a number'");
    EXPECT_FALSE(vm.GetGlobal<int>("s").has_value());
    EXPECT_EQ(vm.GetGlobal<std::string>("s").value_or(""), "not a number");
}

TEST(LuaVMTest, GetGlobalNilReturnsNullopt)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm, "n = nil");
    EXPECT_FALSE(vm.GetGlobal<int>("n").has_value());
}

TEST(LuaVMTest, CallFunction)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm, "function add(a, b) return a + b end");

    auto result = vm.CallFunction("add", 2, 3);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->valid());
    EXPECT_EQ(result->get<int>(), 5);
}

TEST(LuaVMTest, CallFunctionMissingReturnsNullopt)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());

    auto result = vm.CallFunction("no_such_function");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(vm.LastError().empty());
}

TEST(LuaVMTest, CallFunctionLuaErrorReported)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm, "function boom() error('kaboom') end");

    auto result = vm.CallFunction("boom");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->valid());
    EXPECT_NE(result->get<sol::error>().what(), nullptr);
    EXPECT_FALSE(vm.LastError().empty());
}

TEST(LuaVMTest, RegisterFunctionRoundTrip)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());

    int call_count = 0;
    ASSERT_TRUE(vm.RegisterFunction("cpp_tick", [&call_count]() { ++call_count; }));

    ExecuteOk(vm, "cpp_tick()");
    ExecuteOk(vm, "cpp_tick()");
    EXPECT_EQ(call_count, 2);
}

TEST(LuaVMTest, SetGlobalReadBack)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());

    ASSERT_TRUE(vm.SetGlobal("answer", 42));
    EXPECT_EQ(vm.GetGlobal<int>("answer").value_or(-1), 42);
}

TEST(LuaVMTest, OperationsBeforeInitializeFail)
{
    LuaVM vm;
    EXPECT_FALSE(vm.RegisterFunction("f", []() {}));
    EXPECT_FALSE(vm.SetGlobal("g", 1));
    EXPECT_FALSE(vm.ExecuteString("x = 1"));
    EXPECT_FALSE(vm.GetGlobal<int>("x").has_value());
    EXPECT_FALSE(vm.CallFunction("f").has_value());
}

TEST(LuaVMTest, SandboxStripsNativeModuleLoading)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    ExecuteOk(vm,
        "sandbox_ok = (package.loadlib == nil) and (package.cpath == nil) "
        "and (package.path ~= nil) and (os == nil) and (io == nil)");
    EXPECT_TRUE(vm.GetGlobal<bool>("sandbox_ok").value_or(false));
}

TEST(LuaVMTest, InstructionLimitAbortsRunawayScript)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize(100'000)); // finite budget
    ExecuteOk(vm, "function spin() while true do end end");

    // The spin loop must be aborted by the hook, not hang the test.
    auto result = vm.CallFunction("spin");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->valid());
    EXPECT_FALSE(vm.LastError().empty());
}

TEST(LuaVMTest, ZeroInstructionLimitMeansUnlimited)
{
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize(0));
    ExecuteOk(vm, "function count_up(n) local i = 0 while i < n do i = i + 1 end return i end");

    auto result = vm.CallFunction("count_up", 10'000);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->valid());
    EXPECT_EQ(result->get<int>(), 10'000);
}
