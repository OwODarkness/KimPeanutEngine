#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "command/command_registry.h"
#include "script/command/lua_command_bridge.h"
#include "script/lua/lua_vm.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"

namespace
{
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCategory;
    using kpengine::runtime::command::CommandDesc;
    using kpengine::runtime::command::CommandFlags;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandResult;
    using kpengine::runtime::command::CommandStatus;
    using kpengine::runtime::command::CommandThread;
    using kpengine::runtime::script::LuaCommandBridge;
    using kpengine::script::lua::LuaVM;

    class FakeCaptureService final : public kpengine::render::IRenderCaptureService
    {
    public:
        kpengine::render::CapturedImageCallback pending_callback;

        bool RequestCapture(kpengine::render::CaptureRequest,
                            kpengine::render::CapturedImageCallback on_completed) override
        {
            pending_callback = std::move(on_completed);
            return true;
        }

        void Complete()
        {
            ASSERT_TRUE(pending_callback);
            pending_callback({kpengine::render::CaptureResultStatus::Captured,
                              {1, 1, 1, 4, {10, 20, 30, 255}}, {}});
            pending_callback = {};
        }
    };

    void RemoveFile(const std::string& path)
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

TEST(LuaCommandBridgeTest, ListsExecutesAndPollsLuaAllowedGameCommands)
{
    CommandRegistry registry;
    int execution_count = 0;
    const auto registration = registry.Register(CommandDesc{
        "test.lua_echo", "LuaCommandBridgeTest", "Echo a Lua value", CommandCategory::Test,
        CommandFlags::LuaAllowed,
        {{kpengine::runtime::command::CommandArgumentDesc{
            "value", kpengine::runtime::command::CommandValueType::SignedInteger, true, {}, {}}}},
        [&execution_count](const CommandCall& call, const auto&)
        {
            ++execution_count;
            return CommandResult{CommandStatus::Success, "done", 0,
                                 {{"value", call.arguments.at("value")}}};
        },
        CommandThread::Game});
    ASSERT_TRUE(registration.IsSuccess());

    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    LuaCommandBridge bridge{registry, vm};
    ASSERT_TRUE(bridge.Initialize());

    ASSERT_TRUE(vm.ExecuteString(
        "lua_count = #engine.command.list()\n"
        "lua_help_name = engine.command.help('test.lua_echo').command.name\n"
        "lua_submit = engine.command.execute('test.lua_echo', { value = 7 })\n"
        "lua_submit_status = lua_submit.status\n"
        "lua_request_id = lua_submit.request_id\n")) << vm.LastError();
    EXPECT_GE(vm.GetGlobal<int>("lua_count").value_or(0), 3);
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_help_name").value_or(""), "test.lua_echo");
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_submit_status").value_or(""), "pending");
    EXPECT_EQ(execution_count, 0);

    EXPECT_EQ(registry.PumpGameThread(), 1U);
    EXPECT_EQ(execution_count, 1);
    ASSERT_TRUE(vm.ExecuteString("lua_done = engine.command.poll(lua_request_id)")) << vm.LastError();
    ASSERT_TRUE(vm.ExecuteString("lua_done_status = lua_done.status; lua_done_value = lua_done.data.value"))
        << vm.LastError();
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_done_status").value_or(""), "success");
    EXPECT_EQ(vm.GetGlobal<int64_t>("lua_done_value").value_or(0), 7);

    bridge.Shutdown();
    ASSERT_TRUE(vm.ExecuteString("lua_bridge_released = engine.command.execute == nil")) << vm.LastError();
    EXPECT_TRUE(vm.GetGlobal<bool>("lua_bridge_released").value_or(false));
}

TEST(LuaCommandBridgeTest, RejectsCommandsWithoutLuaPermission)
{
    CommandRegistry registry;
    const auto registration = registry.Register(CommandDesc{
        "test.native_only", "LuaCommandBridgeTest", "", CommandCategory::Test, {}, {},
        [](const CommandCall&, const auto&)
        { return CommandResult{CommandStatus::Success, "", 0, {}}; }});
    ASSERT_TRUE(registration.IsSuccess());

    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    LuaCommandBridge bridge{registry, vm};
    ASSERT_TRUE(bridge.Initialize());
    ASSERT_TRUE(vm.ExecuteString(
        "lua_native_only = engine.command.execute('test.native_only').status\n"
        "lua_hidden = engine.command.help('test.native_only').status\n")) << vm.LastError();
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_native_only").value_or(""), "denied");
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_hidden").value_or(""), "not_found");
}

TEST(LuaCommandBridgeTest, CompletesPendingScreenshotAndSurvivesVmShutdown)
{
    FakeCaptureService capture_service;
    auto screenshot_service = std::make_shared<kpengine::runtime::RuntimeScreenshotService>(capture_service);
    CommandRegistry registry;
    const auto registration = kpengine::runtime::RegisterScreenshotCommands(registry, screenshot_service);
    ASSERT_TRUE(registration.IsSuccess());

    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    LuaCommandBridge bridge{registry, vm};
    ASSERT_TRUE(bridge.Initialize());
    const std::string output_path = "save/screenshots/validation/lua-command-bridge-test.png";
    RemoveFile(output_path);
    ASSERT_TRUE(vm.ExecuteString(
        "lua_capture = engine.command.execute('capture.screenshot', {"
        "path = 'save/screenshots/validation/lua-command-bridge-test.png', view = 'scene_color' })\n"
        "lua_capture_id = lua_capture.request_id\n"
        "lua_capture_status = lua_capture.status\n")) << vm.LastError();
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_capture_status").value_or(""), "pending");
    const uint64_t request_id = vm.GetGlobal<uint64_t>("lua_capture_id").value_or(0);
    ASSERT_NE(request_id, 0U);

    EXPECT_EQ(registry.PumpGameThread(), 1U);
    capture_service.Complete();
    ASSERT_TRUE(vm.ExecuteString("lua_capture_done = engine.command.poll(lua_capture_id)")) << vm.LastError();
    ASSERT_TRUE(vm.ExecuteString("lua_capture_done_status = lua_capture_done.status")) << vm.LastError();
    EXPECT_EQ(vm.GetGlobal<std::string>("lua_capture_done_status").value_or(""), "success");
    EXPECT_TRUE(std::filesystem::exists(output_path));
    RemoveFile(output_path);

    ASSERT_TRUE(vm.ExecuteString(
        "lua_shutdown_capture = engine.command.execute('capture.screenshot', {"
        "path = 'save/screenshots/validation/lua-command-bridge-shutdown-test.png' })\n"
        "lua_shutdown_capture_id = lua_shutdown_capture.request_id\n")) << vm.LastError();
    const uint64_t shutdown_request_id = vm.GetGlobal<uint64_t>("lua_shutdown_capture_id").value_or(0);
    ASSERT_NE(shutdown_request_id, 0U);
    ASSERT_EQ(registry.PumpGameThread(), 1U);
    bridge.Shutdown();
    vm.Shutdown();
    registry.Shutdown();
    const auto completion = registry.TakeCompletion(shutdown_request_id);
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->status, CommandStatus::Shutdown);
    capture_service.Complete(); // Late provider completion must be ignored.
    EXPECT_FALSE(registry.TakeCompletion(shutdown_request_id).has_value());
    RemoveFile("save/screenshots/validation/lua-command-bridge-shutdown-test.png");
}
