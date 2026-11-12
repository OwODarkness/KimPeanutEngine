#include "editor_ui/ui_plot.h"
#include "editor_ui/ui_recording_backend.h"
#include "editor_ui/ui_session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

    TEST(UiRecordingContractTest, SnapshotDescribesTableCellsAndPlottedSignal)
    {
        UiSession session(std::make_unique<UiRecordingBackend>());
        ASSERT_TRUE(session.Initialize({}));
        ASSERT_TRUE(session.BeginFrame({.observe = true}));
        auto &ui = session.Context();
        {
            auto window = ui.Window({.id = StableUiNodeId("player"), .label = "Player"});
            auto panel = ui.Node({.id = "queue", .label = "Queue"}, UiRole::Panel, true);
            constexpr std::array<UiTableColumn, 2> columns{{{"Track"}, {"Duration"}}};
            ui.Table("tracks", columns, 0, true, [&]
            {
                ui.TableRow("track.7", true, "A track");
                ui.TableColumn();
                (void)ui.Selectable({.id = "select.7", .label = "A track"}, true);
                ui.TableColumn();
                ui.Text("03:14");
            }, "Playback queue");
            constexpr std::array<UiPlotPoint, 1> samples{{{100.0, -12.0}}};
            const std::array<UiPlotSeries, 1> series{{
                {.label = "Signal", .points = samples, .kind = UiPlotSeriesKind::Bars}}};
            (void)Plot(ui, {.control = {.id = "spectrum", .label = "Spectrum"},
                .size = {240, 160},
                .x_axis = {.minimum = 10, .maximum = 20000, .scale = UiPlotScale::Log10,
                           .unit = "Hz", .semantic_label = "Frequency"},
                .y_axis = {.minimum = -60, .maximum = 0, .unit = "dB"},
                .series = series});
        }
        const auto frame = session.EndFrame();
        const auto find = [&frame](const std::string_view key)
        {
            return std::find_if(frame.observations.begin(), frame.observations.end(),
                [key](const UiNodeObservation &node) { return node.semantic_key == key; });
        };
        const auto table = find("tracks");
        const auto row = find("track.7");
        const auto cell = find("select.7");
        const auto chart = find("spectrum");
        ASSERT_NE(table, frame.observations.end());
        ASSERT_NE(row, frame.observations.end());
        ASSERT_NE(cell, frame.observations.end());
        ASSERT_NE(chart, frame.observations.end());
        EXPECT_EQ(table->parent, StableUiNodeId("queue"));
        EXPECT_EQ(table->table_columns, (std::vector<std::string>{"Track", "Duration"}));
        EXPECT_EQ(row->label, "A track");
        EXPECT_TRUE(row->selected);
        EXPECT_EQ(cell->parent, row->id);
        EXPECT_EQ(cell->table_column, 0U);
        const auto duration = std::find_if(frame.observations.begin(), frame.observations.end(),
            [](const UiNodeObservation &node) { return node.label == "03:14"; });
        ASSERT_NE(duration, frame.observations.end());
        EXPECT_EQ(duration->parent, row->id);
        EXPECT_EQ(duration->table_column, 1U);
        EXPECT_EQ(chart->role, UiRole::Chart);
        ASSERT_TRUE(chart->chart);
        EXPECT_EQ(chart->chart->x.unit, "Hz");
        ASSERT_EQ(chart->chart->series.size(), 1U);
        ASSERT_EQ(chart->chart->series.front().points.size(), 1U);
        EXPECT_DOUBLE_EQ(chart->chart->series.front().points.front().y, -12.0);
    }

    TEST(UiRecordingContractTest, SemanticQueriesExplainAnUnfamiliarInterface)
    {
        auto backend = std::make_unique<UiRecordingBackend>();
        auto *const recording = backend.get();
        const UiNodeId window_id = StableUiNodeId("demo.window");
        const UiNodeId panel_id = StableUiNodeId("demo.playback");
        const UiNodeId play_id = StableUiNodeId("playback.play");
        const UiNodeId volume_id = StableUiNodeId("playback.volume");
        const UiNodeId mute_id = StableUiNodeId("playback.mute");
        recording->SetFixtureBounds(window_id, {{0.0F, 0.0F}, {480.0F, 320.0F}});
        recording->SetFixtureBounds(volume_id, {{24.0F, 88.0F}, {456.0F, 112.0F}});

        UiSession session(std::move(backend));
        ASSERT_TRUE(session.Initialize({{480.0F, 320.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}}));
        ASSERT_TRUE(session.BeginFrame({.observe = true,
            .viewport = {{480.0F, 320.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}}}));
        UiContext &ui = session.Context();
        UiTheme theme{};
        theme.accent = {0.10F, 0.80F, 0.95F, 1.0F};
        {
            auto theme_scope = ui.Theme(theme);
            auto window = ui.Window({.id = window_id, .label = "Control Surface"});
            auto panel = ui.Node({.id = "demo.playback", .label = "Playback"}, UiRole::Panel, true);
            (void)ui.Button({.id = "playback.play", .label = "Play"});
            (void)ui.Slider<double>({.id = "playback.volume", .label = "Master volume",
                    .unit = "%"}, 72.0, 0.0, 100.0);
            (void)ui.Checkbox({.id = "playback.mute", .label = "Mute"}, false);
        }
        const UiFrameResult frame = session.EndFrame();
        ASSERT_TRUE(frame.complete);

        const UiSemanticView semantic = session.Query();
        ASSERT_TRUE(semantic.IsValid());
        const UiSemanticTree &tree = semantic.GetUITree();
        ASSERT_TRUE(tree.complete);
        ASSERT_EQ(tree.session, semantic.SessionId());
        ASSERT_EQ(tree.frame, frame.frame);
        ASSERT_EQ(tree.viewport.logical_extent.x, 480.0F);
        ASSERT_EQ(tree.viewport.logical_extent.y, 320.0F);
        ASSERT_EQ(tree.roots.size(), 1U);

        const auto find_tree_node = [&tree](const UiNodeId id) -> const UiSemanticTreeNode *
        {
            const auto found = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                [id](const UiSemanticTreeNode &node) { return node.id == id; });
            return found == tree.nodes.end() ? nullptr : &*found;
        };
        const UiSemanticTreeNode *const root = find_tree_node(tree.roots.front());
        const UiSemanticTreeNode *const window = find_tree_node(window_id);
        const UiSemanticTreeNode *const panel = find_tree_node(panel_id);
        ASSERT_NE(root, nullptr);
        ASSERT_NE(window, nullptr);
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(root->role, UiRole::Session);
        EXPECT_EQ(root->children, (std::vector<UiNodeId>{window_id}));
        EXPECT_EQ(window->role, UiRole::Window);
        EXPECT_EQ(window->label, "Control Surface");
        EXPECT_EQ(panel->role, UiRole::Panel);
        EXPECT_EQ(panel->label, "Playback");
        ASSERT_TRUE(panel->selected);
        EXPECT_TRUE(*panel->selected);
        EXPECT_EQ(panel->geometry, UiGeometryProvenance::DerivedFromChildren);
        EXPECT_EQ(panel->bounds.min.x, 24.0F);
        EXPECT_EQ(panel->bounds.max.y, 112.0F);
        EXPECT_EQ(window->children, (std::vector<UiNodeId>{panel_id}));
        for (const UiNodeId child : {play_id, volume_id, mute_id})
            EXPECT_TRUE(std::find(panel->children.begin(), panel->children.end(), child) !=
                        panel->children.end());

        const auto play = semantic.GetElementInfo(play_id);
        ASSERT_TRUE(play);
        ASSERT_NE(play->element, nullptr);
        EXPECT_EQ(play->element->role, UiRole::Button);
        EXPECT_EQ(play->element->label, "Play");
        EXPECT_FALSE(play->element->value.has_value());
        EXPECT_FALSE(play->element->selected.has_value());
        EXPECT_FALSE(play->element->active.has_value());
        ASSERT_EQ(play->element->actions.size(), 1U);
        EXPECT_EQ(play->element->actions.front(), UiActionKind::Activate);

        const auto volume = semantic.GetElementInfo(volume_id);
        ASSERT_TRUE(volume);
        ASSERT_NE(volume->element, nullptr);
        EXPECT_EQ(volume->element->role, UiRole::Slider);
        EXPECT_EQ(volume->element->label, "Master volume");
        EXPECT_EQ(volume->element->unit, "%");
        ASSERT_TRUE(volume->element->value);
        EXPECT_DOUBLE_EQ(std::get<double>(*volume->element->value), 72.0);
        ASSERT_TRUE(volume->element->minimum);
        ASSERT_TRUE(volume->element->maximum);
        EXPECT_DOUBLE_EQ(std::get<double>(*volume->element->minimum), 0.0);
        EXPECT_DOUBLE_EQ(std::get<double>(*volume->element->maximum), 100.0);
        EXPECT_EQ(volume->element->bounds.min.x, 24.0F);
        EXPECT_EQ(volume->element->bounds.max.y, 112.0F);
        EXPECT_EQ(volume->element->geometry, UiGeometryProvenance::TestFixture);
        EXPECT_TRUE(volume->element->enabled);
        ASSERT_TRUE(volume->element->visible);
        EXPECT_TRUE(*volume->element->visible);
        EXPECT_EQ(volume->element->clipped_bounds->min.x, 24.0F);

        const auto mute = semantic.GetElementInfo(mute_id);
        ASSERT_TRUE(mute);
        EXPECT_EQ(mute->element->role, UiRole::CheckBox);
        ASSERT_TRUE(mute->element->value);
        EXPECT_FALSE(std::get<bool>(*mute->element->value));
        EXPECT_FALSE(mute->element->selected.has_value());
        EXPECT_FALSE(mute->element->focused.has_value());
        ASSERT_EQ(mute->element->actions.size(), 1U);
        EXPECT_EQ(mute->element->actions.front(), UiActionKind::Toggle);

        const UiElementStyle *const style = semantic.GetElementStyle(volume_id);
        ASSERT_NE(style, nullptr);
        EXPECT_EQ(style->declared.accent, theme.accent);
        EXPECT_FALSE(style->has_resolved_values);
        EXPECT_EQ(semantic.GetElementStyle(StableUiNodeId("missing.element")), nullptr);

        UiTheme next_theme = theme;
        next_theme.accent = {0.95F, 0.20F, 0.25F, 1.0F};
        ASSERT_TRUE(session.BeginFrame({.observe = true,
            .viewport = {{480.0F, 320.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}}}));
        {
            auto theme_scope = session.Context().Theme(next_theme);
            auto window_scope = session.Context().Window(
                {.id = window_id, .label = "Control Surface"});
            auto panel_scope = session.Context().Node(
                {.id = "demo.playback", .label = "Playback"}, UiRole::Panel, true);
            (void)session.Context().Slider<double>({.id = "playback.volume",
                .label = "Master volume", .unit = "%"}, 30.0, 0.0, 100.0);
        }
        const UiFrameResult next_frame = session.EndFrame();
        const UiSemanticView next_semantic = session.Query();
        ASSERT_TRUE(next_semantic.IsValid());
        EXPECT_EQ(semantic.FrameId(), frame.frame);
        EXPECT_EQ(next_semantic.FrameId(), next_frame.frame);
        const auto retained_volume = semantic.GetElementInfo(volume_id);
        const auto next_volume = next_semantic.GetElementInfo(volume_id);
        ASSERT_TRUE(retained_volume);
        ASSERT_TRUE(next_volume);
        ASSERT_TRUE(retained_volume->element->value);
        ASSERT_TRUE(next_volume->element->value);
        EXPECT_DOUBLE_EQ(std::get<double>(*retained_volume->element->value), 72.0);
        EXPECT_DOUBLE_EQ(std::get<double>(*next_volume->element->value), 30.0);
        ASSERT_NE(semantic.GetElementStyle(volume_id), nullptr);
        ASSERT_NE(next_semantic.GetElementStyle(volume_id), nullptr);
        EXPECT_EQ(semantic.GetElementStyle(volume_id)->declared.accent, theme.accent);
        EXPECT_EQ(next_semantic.GetElementStyle(volume_id)->declared.accent, next_theme.accent);
    }

    TEST(UiRecordingContractTest, QueryLevelsFilterAndReportBoundedFrameChanges)
    {
        UiSession session(std::make_unique<UiRecordingBackend>());
        ASSERT_TRUE(session.Initialize({}));
        const UiNodeId root = StableUiNodeId("query.root");
        const UiNodeId value = StableUiNodeId("query.value");
        const auto build = [&session, root, value](const double number, const bool add_extra)
        {
            if (!session.BeginFrame({.observe = true,
                .viewport = {{640.0F, 480.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}}})) return UiFrameResult{};
            UiTheme theme{};
            theme.accent.r = static_cast<float>(number / 10.0);
            {
                auto theme_scope = session.Context().Theme(theme);
                auto window = session.Context().Window({.id = root, .label = "Query"});
                (void)session.Context().Slider<double>({.id = "query.value", .label = "Value"},
                    number, 0.0, 10.0);
                if (add_extra) (void)session.Context().Button({.id = "query.extra", .label = "Extra"});
            }
            return session.EndFrame();
        };

        const UiFrameResult first = build(2.0, false);
        const UiSemanticView first_view = session.Query();
        const auto semantic = first_view.Query({.level = UiQueryLevel::Semantic,
            .semantic_id = "query.value"});
        ASSERT_EQ(semantic.status, UiQueryStatus::Ok);
        ASSERT_EQ(semantic.elements.size(), 1U);
        EXPECT_EQ(semantic.elements.front().element_id, value);
        EXPECT_FALSE(semantic.elements.front().bounds.has_value());
        EXPECT_FALSE(semantic.viewport.has_value());
        const auto spatial = first_view.Query({.level = UiQueryLevel::Spatial,
            .element_id = root, .role = UiRole::Slider, .descendants = true});
        ASSERT_EQ(spatial.status, UiQueryStatus::Ok);
        ASSERT_EQ(spatial.elements.size(), 1U);
        ASSERT_TRUE(spatial.viewport.has_value());
        const auto visual = first_view.Query({.level = UiQueryLevel::Visual,
            .element_id = value, .descendants = false});
        ASSERT_EQ(visual.status, UiQueryStatus::Ok);
        EXPECT_EQ(visual.styles.size(), 1U);
        ASSERT_TRUE(visual.elements.front().style_id.has_value());

        const UiFrameResult second = build(4.0, true);
        const UiSemanticChanges delta = session.ChangesSince(first_view.SessionId(), first.frame);
        ASSERT_EQ(delta.status, UiChangesStatus::Ok);
        EXPECT_EQ(delta.to_frame, second.frame);
        EXPECT_TRUE(std::any_of(delta.changes.begin(), delta.changes.end(),
            [](const auto &change) { return change.kind == UiChangeKind::Added; }));
        EXPECT_TRUE(std::any_of(delta.changes.begin(), delta.changes.end(),
            [value](const auto &change) { return change.element_id == value &&
                (change.fields & static_cast<std::uint16_t>(UiChangeField::Value)) != 0; }));
        const auto value_change = std::find_if(delta.changes.begin(), delta.changes.end(),
            [value](const auto &change) { return change.element_id == value; });
        ASSERT_NE(value_change, delta.changes.end());
        ASSERT_TRUE(value_change->current.has_value());
        ASSERT_TRUE(value_change->current->value.has_value());
        EXPECT_DOUBLE_EQ(std::get<double>(*value_change->current->value), 4.0);
        EXPECT_TRUE(std::any_of(delta.changes.begin(), delta.changes.end(),
            [value](const auto &change) { return change.element_id == value &&
                (change.fields & static_cast<std::uint16_t>(UiChangeField::Style)) != 0; }));

        const UiFrameResult third = build(4.0, false);
        const UiSemanticChanges removal = session.ChangesSince(first_view.SessionId(), second.frame);
        ASSERT_EQ(removal.status, UiChangesStatus::Ok);
        EXPECT_EQ(removal.to_frame, third.frame);
        EXPECT_TRUE(std::any_of(removal.changes.begin(), removal.changes.end(),
            [](const auto &change) { return change.kind == UiChangeKind::Removed; }));
        EXPECT_EQ(session.ChangesSince(first_view.SessionId() + 1, second.frame).status,
                  UiChangesStatus::FullSnapshotRequired);

        ASSERT_TRUE(session.BeginFrame({.observe = true}));
        const UiFrameResult empty = session.EndFrame();
        const UiSemanticChanges cleared = session.ChangesSince(first_view.SessionId(), third.frame);
        ASSERT_EQ(cleared.status, UiChangesStatus::Ok);
        EXPECT_EQ(cleared.to_frame, empty.frame);
        EXPECT_EQ(cleared.changes.size(), 2U);
        ASSERT_TRUE(session.BeginFrame({}));
        const UiFrameResult unobserved = session.EndFrame();
        EXPECT_FALSE(unobserved.observed);
        const UiFrameResult restored = build(4.0, false);
        EXPECT_EQ(session.ChangesSince(first_view.SessionId(), empty.frame).status,
                  UiChangesStatus::Ok);
        for (int i = 0; i < 34; ++i) (void)build(4.0, false);
        EXPECT_EQ(session.ChangesSince(first_view.SessionId(), restored.frame).status,
                  UiChangesStatus::FullSnapshotRequired);
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
        ASSERT_TRUE(observations[1].value);
        EXPECT_EQ(std::get<std::string>(*observations[1].value), "edited value");
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
