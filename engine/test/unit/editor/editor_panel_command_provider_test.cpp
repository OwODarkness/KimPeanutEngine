#include "editor/ui/editor_panel_command_provider.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using kpengine::editor::EditorPanelCommandKind;
    using kpengine::editor::EditorPanelCommandRequest;
    using kpengine::editor::EditorPanelCommandRegistrationResult;
    using kpengine::editor::RegisterEditorPanelCommands;
    using kpengine::runtime::command::CommandCall;
    using kpengine::runtime::command::CommandCapability;
    using kpengine::runtime::command::CommandContext;
    using kpengine::runtime::command::CommandOrigin;
    using kpengine::runtime::command::CommandRegistry;
    using kpengine::runtime::command::CommandResult;
    using kpengine::runtime::command::CommandStatus;
    using kpengine::runtime::command::CommandThread;

    struct Queue final
    {
        bool Enqueue(EditorPanelCommandRequest request)
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<EditorPanelCommandRequest> requests;
    };
}

TEST(EditorPanelCommandProviderTest, RegistersListShowAndFocus)
{
    CommandRegistry registry;
    Queue queue;
    const EditorPanelCommandRegistrationResult result = RegisterEditorPanelCommands(
        registry, [&queue](EditorPanelCommandRequest request)
        { return queue.Enqueue(std::move(request)); });

    ASSERT_TRUE(result.succeeded) << result.diagnostic;
    ASSERT_TRUE(registry.Find("editor.panel.list").has_value());
    ASSERT_TRUE(registry.Find("editor.panel.show").has_value());
    ASSERT_TRUE(registry.Find("editor.panel.focus").has_value());
}

TEST(EditorPanelCommandProviderTest, DeferredAgentCallReachesTheRenderQueue)
{
    CommandRegistry registry;
    Queue queue;
    const EditorPanelCommandRegistrationResult registration = RegisterEditorPanelCommands(
        registry, [&queue](EditorPanelCommandRequest request)
        { return queue.Enqueue(std::move(request)); });
    ASSERT_TRUE(registration.succeeded);

    const CommandResult pending = registry.Execute(
        CommandCall{"editor.panel.show", {{"id", std::string{"asset_browser"}}}},
        CommandContext{CommandOrigin::Agent, CommandThread::Immediate,
                       CommandCapability::Mutating});
    ASSERT_EQ(pending.status, CommandStatus::Pending);
    ASSERT_NE(pending.request_id, 0u);

    EXPECT_EQ(registry.PumpGameThread(), 1u);
    ASSERT_EQ(queue.requests.size(), 1u);
    EXPECT_EQ(queue.requests.front().kind, EditorPanelCommandKind::Show);
    EXPECT_EQ(queue.requests.front().id, "asset_browser");

    queue.requests.front().completion(
        {CommandStatus::Success, "Editor panel shown", 0, {}});
    const std::optional<CommandResult> completed = registry.TakeCompletion(pending.request_id);
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->status, CommandStatus::Success);
}

TEST(EditorPanelCommandProviderTest, ShowRequiresTheMutatingCapability)
{
    CommandRegistry registry;
    Queue queue;
    const EditorPanelCommandRegistrationResult registration = RegisterEditorPanelCommands(
        registry, [&queue](EditorPanelCommandRequest request)
        { return queue.Enqueue(std::move(request)); });
    ASSERT_TRUE(registration.succeeded);

    const CommandResult result = registry.Execute(
        CommandCall{"editor.panel.show", {{"id", std::string{"asset_browser"}}}},
        CommandContext{CommandOrigin::Agent, CommandThread::Immediate});
    EXPECT_EQ(result.status, CommandStatus::Denied);
    EXPECT_TRUE(queue.requests.empty());
}

TEST(EditorPanelCommandProviderTest, PanelIdIsRequired)
{
    CommandRegistry registry;
    Queue queue;
    const EditorPanelCommandRegistrationResult registration = RegisterEditorPanelCommands(
        registry, [&queue](EditorPanelCommandRequest request)
        { return queue.Enqueue(std::move(request)); });
    ASSERT_TRUE(registration.succeeded);

    const CommandResult result = registry.Execute(
        CommandCall{"editor.panel.focus", {}},
        CommandContext{CommandOrigin::Agent, CommandThread::Immediate,
                       CommandCapability::Mutating});
    EXPECT_EQ(result.status, CommandStatus::InvalidArguments);
    EXPECT_TRUE(queue.requests.empty());
}
