#include "editor_ui/ui_recording_backend.h"
#include "editor_ui/ui_session.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace kpengine::editor::ui
{
    namespace
    {
        class FailingBackend final : public IUiBackend
        {
        public:
            bool Initialize(const UiViewportInfo &) override { return false; }
            void BeginFrame(const UiFrameInfo &) override {}
            bool BeginWindow(const UiControl &) override { return false; }
            void EndWindow() override {}
            void SetHostContainer(const UiControl &) override {}
            UiEditResult Control(const UiControl &control) override { return {control.value}; }
            void Text(std::string_view) override {}
            UiFrameResult EndFrame(std::uint64_t frame) override { return {frame}; }
            void AbortFrame() noexcept override {}
            void Shutdown() noexcept override { ++shutdown_calls; }
            int shutdown_calls = 0;
        };

        class InvalidComboBackend final : public IUiBackend
        {
        public:
            bool Initialize(const UiViewportInfo &) override { return true; }
            void BeginFrame(const UiFrameInfo &) override {}
            bool BeginWindow(const UiControl &) override { return true; }
            void EndWindow() override {}
            void SetHostContainer(const UiControl &) override {}
            UiEditResult Control(const UiControl &) override
            {
                return {std::string{"not-a-choice"}, true, true, false, true,
                        UiActionKind::Select};
            }
            void Text(std::string_view) override {}
            UiFrameResult EndFrame(const std::uint64_t frame) override { return {frame, true}; }
            void AbortFrame() noexcept override { ++abort_calls; }
            void Shutdown() noexcept override {}
            int abort_calls = 0;
        };
    }

    TEST(UiRecordingContractTest, ReplaysTypedInputAndCopiesSemanticObservation)
    {
        auto backend = std::make_unique<UiRecordingBackend>(
            std::vector<UiScriptedInput>{{11, std::uint64_t{9007199254740993ULL}}});
        auto *const recording = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));

        UiFrameInfo frame{};
        ASSERT_TRUE(session.BeginFrame(frame));
        ASSERT_TRUE(session.Context().BeginWindow(
            {.id = 1, .role = UiRole::Group, .label = "Settings"}));
        const UiEditResult edit = session.Context().Control(
            {.id = 11, .role = UiRole::NumericInput, .label = "Large integer",
             .value = std::uint64_t{9007199254740992ULL},
             .actions = {UiActionKind::Edit, UiActionKind::Commit}});
        session.Context().EndWindow();
        const UiFrameResult result = session.EndFrame();

        EXPECT_TRUE(result.complete);
        EXPECT_TRUE(edit.changed);
        EXPECT_EQ(std::get<std::uint64_t>(edit.value), 9007199254740993ULL);
        const auto observations = recording->Observations();
        ASSERT_EQ(observations.size(), 2U);
        EXPECT_EQ(observations[1].label, "Large integer");
        EXPECT_EQ(observations[1].geometry, UiGeometryProvenance::Unavailable);
    }

    TEST(UiRecordingContractTest, CompletedObservationOwnsTemporaryControlValues)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recording = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        const UiSessionId session_id = session.Context().SessionId();
        recording->QueueInput(session_id, 1, 2, std::string{"edited value"},
                              UiActionKind::Edit);

        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window(
                {.id = 1, .label = std::string{"Temporary window label"}});
            {
                std::string label{"Temporary control label"};
                std::string description{"Temporary control description"};
                std::string value{"original value"};
                const UiControl control{.id = 2,
                                        .role = UiRole::TextInput,
                                        .label = label,
                                        .description = description,
                                        .value = value,
                                        .actions = {UiActionKind::Edit,
                                                    UiActionKind::Commit,
                                                    UiActionKind::Cancel}};
                const UiEditResult edit = session.Context().Control(control);
                ASSERT_TRUE(edit.changed);
                ASSERT_EQ(std::get<std::string>(edit.value), "edited value");
                label.assign("mutated label");
                description.assign("mutated description");
                value.assign("mutated value");
            }
        }
        ASSERT_TRUE(session.EndFrame().complete);

        const auto observations = recording->Observations();
        ASSERT_EQ(observations.size(), 2U);
        EXPECT_EQ(observations[0].label, "Temporary window label");
        EXPECT_EQ(observations[1].label, "Temporary control label");
        EXPECT_EQ(observations[1].description, "Temporary control description");
        EXPECT_EQ(std::get<std::string>(observations[1].value), "edited value");
        EXPECT_EQ(observations[1].parent, 1U);
    }

    TEST(UiRecordingContractTest, TypedControlsUseStableStringIdsAndReturnTypedResults)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recorder = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        const UiSessionId actual_session = session.Context().SessionId();
        const std::vector<UiScriptedInput> input{
            {actual_session, 1, StableUiNodeId("settings.apply"), false, UiActionKind::Activate},
            {actual_session, 1, StableUiNodeId("settings.fog"), true, UiActionKind::Toggle},
            {actual_session, 1, StableUiNodeId("assets.material"), true, UiActionKind::Select},
            {actual_session, 1, StableUiNodeId("render.quality"), std::string{"high"}, UiActionKind::Select},
            {actual_session, 1, StableUiNodeId("render.samples"), std::int64_t{8}, UiActionKind::Edit},
            {actual_session, 1, StableUiNodeId("project.name"), std::string{"new"}, UiActionKind::Edit}};
        recorder->QueueInput(input[0].session, input[0].frame, input[0].id,
                             input[0].value, input[0].action);
        for (std::size_t i = 1; i < input.size(); ++i)
            recorder->QueueInput(input[i].session, input[i].frame, input[i].id,
                                 input[i].value, input[i].action);

        auto frame = session.Frame({});
        {
            auto window = session.Context().Window({.id = 1, .label = "Settings"});
            const auto button = session.Context().Button({.id = "settings.apply", .label = "Apply"});
            const auto checkbox = session.Context().Checkbox(
                {.id = "settings.fog", .label = "Fog"}, false);
            const auto selectable = session.Context().Selectable(
                {.id = "assets.material", .label = "Material"}, false);
            const auto combo = session.Context().Combo(
                {.id = "render.quality", .label = "Quality"}, std::string{"low"},
                {{"low", "Low", std::string{"low"}},
                 {"high", "High", std::string{"high"}}});
            const auto numeric = session.Context().NumericInput(
                {.id = "render.samples", .label = "Samples"}, std::int64_t{4},
                std::int64_t{1}, std::int64_t{16}, std::int64_t{1});
            const auto text = session.Context().TextInput(
                {.id = "project.name", .label = "Name"}, "old");

            EXPECT_TRUE(button.activated);
            EXPECT_TRUE(checkbox.changed);
            bool fog_enabled = false;
            if (checkbox.changed)
                fog_enabled = checkbox.value;
            EXPECT_TRUE(fog_enabled);
            EXPECT_TRUE(selectable.activated);
            EXPECT_TRUE(selectable.value);
            EXPECT_EQ(combo.value, "high");
            EXPECT_EQ(numeric.value, 8);
            EXPECT_EQ(text.value, "new");
        }
        EXPECT_EQ(recorder->Operations().size(), 8U);
        EXPECT_TRUE(frame.End().complete);
    }

    TEST(UiRecordingContractTest, RejectsInvalidTypedControlValuesBeforeBackendDispatch)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recorder = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        auto frame = session.Frame({});
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            EXPECT_THROW(session.Context().NumericInputValue(
                             {.id = "bad.number", .label = "Bad"}, std::string{"not numeric"}),
                         std::logic_error);
            const UiEditResult corrected = session.Context().NumericInputValue(
                {.id = "bad.number", .label = "Corrected"}, std::int64_t{4},
                UiValue{std::int64_t{0}}, UiValue{std::int64_t{8}}, UiValue{std::int64_t{1}});
            EXPECT_EQ(std::get<std::int64_t>(corrected.value), 4);
            EXPECT_THROW(session.Context().Combo(
                             {.id = "bad.combo", .label = "Bad"}, std::int64_t{1},
                             {{"wrong-type", "Wrong type", std::string{"x"}}}),
                         std::logic_error);
        }
        EXPECT_EQ(recorder->Operations().size(), 3U);
        EXPECT_TRUE(frame.End().complete);
    }

    TEST(UiRecordingContractTest, InvalidBackendComboResultAbortsTheFrame)
    {
        auto backend = std::make_unique<InvalidComboBackend>();
        auto *const probe = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        {
            auto frame = session.Frame({});
            {
                auto window = session.Context().Window({.id = 1, .label = "root"});
                EXPECT_THROW(session.Context().Combo(
                                 {.id = "quality", .label = "Quality"}, std::string{"low"},
                                 {{"low", "Low", std::string{"low"}},
                                  {"high", "High", std::string{"high"}}}),
                             std::logic_error);
            }
        }
        EXPECT_EQ(session.State(), UiSessionState::Ready);
        EXPECT_EQ(probe->abort_calls, 1);
    }

    TEST(UiRecordingContractTest, RejectsInputForDisabledControl)
    {
        auto backend = std::make_unique<UiRecordingBackend>(
            std::vector<UiScriptedInput>{{2, true}});
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        session.Context().BeginWindow({.id = 1, .label = "root"});
        EXPECT_THROW(session.Context().Control(
                         {.id = 2, .role = UiRole::CheckBox, .label = "disabled",
                          .enabled = false, .disabled_reason = "not available",
                          .actions = {UiActionKind::Toggle}}),
                     std::logic_error);
    }

    TEST(UiRecordingContractTest, RequiresBalancedWindowScope)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        session.Context().BeginWindow({.id = 1, .label = "root"});
        EXPECT_THROW(session.EndFrame(), std::logic_error);
    }

    TEST(UiRecordingContractTest, FrameScopeAbortsOnExceptionalExitAndAllowsTheNextFrame)
    {
        UiSession session(std::make_unique<UiRecordingBackend>());
        ASSERT_TRUE(session.Initialize({}));

        EXPECT_THROW(
            [&]
            {
                auto frame = session.Frame({});
                {
                    auto window = session.Context().Window({.id = 1, .label = "aborted"});
                }
                throw std::runtime_error("feature callback failed");
            }(),
            std::runtime_error);
        EXPECT_EQ(session.State(), UiSessionState::Ready);

        auto frame = session.Frame({});
        {
            auto window = session.Context().Window({.id = 1, .label = "recovered"});
        }
        EXPECT_TRUE(frame.End().complete);
        EXPECT_EQ(session.State(), UiSessionState::Ready);
    }

    TEST(UiRecordingContractTest, WindowGuardClosesOnScopeExitAndRejectsDuplicateIds)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recording = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            ASSERT_TRUE(window.ContentVisible());
            EXPECT_THROW(session.Context().Control({.id = 1, .label = "duplicate"}),
                         std::logic_error);
        }
        EXPECT_TRUE(session.EndFrame().complete);
        ASSERT_EQ(recording->Operations().size(), 2U);
        EXPECT_EQ(recording->Operations().front().kind, UiOperation::Kind::BeginWindow);
        EXPECT_EQ(recording->Operations().back().kind, UiOperation::Kind::EndWindow);
    }

    TEST(UiRecordingContractTest, RejectsUnmatchedScriptedInput)
    {
        auto backend = std::make_unique<UiRecordingBackend>(
            std::vector<UiScriptedInput>{{999, true}});
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            ASSERT_TRUE(window.ContentVisible());
        }
        EXPECT_THROW(session.EndFrame(), std::logic_error);
    }

    TEST(UiRecordingContractTest, FailedFrameDoesNotReplaceLastCompleteObservation)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recorder = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "frame one"});
        }
        ASSERT_TRUE(session.EndFrame().complete);
        const UiSessionId session_id = session.Context().SessionId();
        recorder->QueueInput(session_id, 2, 999, true, UiActionKind::Toggle);
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 2, .label = "frame two"});
        }
        EXPECT_THROW(session.EndFrame(), std::logic_error);
        const auto observations = recorder->Observations();
        ASSERT_EQ(observations.size(), 1U);
        EXPECT_EQ(observations[0].frame, 1U);
        EXPECT_EQ(observations[0].label, "frame one");
    }

    TEST(UiRecordingContractTest, ClosingRejectsFramesAndShutsDownOnce)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        session.BeginClosing();
        {
            auto window = session.Context().Window({.id = 1, .label = "closing frame"});
            EXPECT_TRUE(window.ContentVisible());
        }
        EXPECT_TRUE(session.EndFrame().complete);
        EXPECT_EQ(session.State(), UiSessionState::Closing);
        EXPECT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "closing presentation"});
            EXPECT_TRUE(window.ContentVisible());
        }
        EXPECT_TRUE(session.EndFrame().complete);
        EXPECT_EQ(session.State(), UiSessionState::Closing);
        session.Shutdown();
        session.Shutdown();
        EXPECT_EQ(session.State(), UiSessionState::Closed);
    }

    TEST(UiRecordingContractTest, FailedInitializationRollsBackBackendOnce)
    {
        auto backend = std::make_unique<FailingBackend>();
        auto *const probe = backend.get();
        UiSession session(std::move(backend));
        EXPECT_FALSE(session.Initialize({}));
        EXPECT_EQ(probe->shutdown_calls, 1);
        session.Shutdown();
        EXPECT_EQ(probe->shutdown_calls, 1);
    }

    TEST(UiRecordingContractTest, RejectsCrossThreadContextUse)
    {
        UiSession session(std::make_unique<UiRecordingBackend>());
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({}));
        std::atomic<bool> rejected = false;
        std::thread worker([&]
        {
            try { session.Context().Text("wrong thread"); }
            catch (const std::logic_error &) { rejected = true; }
        });
        worker.join();
        EXPECT_TRUE(rejected.load());
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
        }
        EXPECT_TRUE(session.EndFrame().complete);
    }

    TEST(UiRecordingContractTest, DistinguishesEditCommitAndCancel)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recorder = backend.get();
        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({}));
        const UiSessionId id = session.Context().SessionId();
        const UiControl input{.id = 2, .role = UiRole::TextInput,
                              .label = "Value", .value = std::string{"1"},
                              .actions = {UiActionKind::Edit, UiActionKind::Commit,
                                          UiActionKind::Cancel}};
        UiControl current = input;

        recorder->QueueInput(id, 1, 2, std::string{"2"}, UiActionKind::Edit);
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            const auto edit = session.Context().Control(current);
            EXPECT_TRUE(edit.changed);
            EXPECT_FALSE(edit.committed);
            current.value = edit.value;
        }
        EXPECT_TRUE(session.EndFrame().complete);

        recorder->QueueInput(id, 2, 2, std::string{"2"}, UiActionKind::Commit);
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            const auto commit = session.Context().Control(current);
            EXPECT_FALSE(commit.changed);
            EXPECT_TRUE(commit.committed);
        }
        EXPECT_TRUE(session.EndFrame().complete);

        recorder->QueueInput(id, 3, 2, std::string{"1"}, UiActionKind::Cancel);
        ASSERT_TRUE(session.BeginFrame({}));
        {
            auto window = session.Context().Window({.id = 1, .label = "root"});
            const auto cancel = session.Context().Control(current);
            EXPECT_FALSE(cancel.changed);
            EXPECT_TRUE(cancel.cancelled);
            EXPECT_EQ(std::get<std::string>(cancel.value), "2");
        }
        EXPECT_TRUE(session.EndFrame().complete);
    }
}
