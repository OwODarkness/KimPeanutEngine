#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "editor/ui/component/editor_tool_row_model.h"

// The tool-row model is ImGui-free by design, so this target links only
// KP::GoogleTest. Everything the model owns — tab order, active-tab
// reconciliation, visibility, dock state, and drop resolution — is covered here;
// the widget behaviour that needs a frame is covered by editor smoke instead.
namespace
{
    using kpengine::editor::EditorRect;
    using kpengine::editor::EditorToolDropTarget;
    using kpengine::editor::EditorToolRowEntry;
    using kpengine::editor::EditorToolRowModel;
    using kpengine::editor::EditorWindowVisibility;
    using kpengine::editor::kToolRowConsoleId;
    using kpengine::editor::kToolRowLogId;
    using kpengine::editor::ResolveToolRowDrop;

    constexpr const char *kUnknownId = "nope";

    std::vector<std::size_t> Detached(const EditorToolRowModel &model)
    {
        return model.GetDetachedIndices();
    }
}

TEST(EditorWindowVisibilityTest, DefaultsClosedAndTracksExplicitState)
{
    EditorWindowVisibility defaulted;
    EXPECT_FALSE(defaulted.IsOpen());

    EditorWindowVisibility opened{true};
    EXPECT_TRUE(opened.IsOpen());

    opened.SetOpen(false);
    EXPECT_FALSE(opened.IsOpen());

    // SetOpen is idempotent; Toggle flips exactly once per call.
    opened.SetOpen(false);
    EXPECT_FALSE(opened.IsOpen());
    opened.Toggle();
    EXPECT_TRUE(opened.IsOpen());
    opened.Toggle();
    EXPECT_FALSE(opened.IsOpen());
}

TEST(EditorToolRowModelTest, RegistrationKeepsOrderAndResolvesIds)
{
    EditorToolRowModel model;
    EXPECT_EQ(model.AddEntry(kToolRowLogId, "Log", true), 0u);
    EXPECT_EQ(model.AddEntry(kToolRowConsoleId, "Console", false), 1u);

    ASSERT_EQ(model.GetEntryCount(), 2u);
    EXPECT_EQ(model.GetEntry(0)->id, "log");
    EXPECT_EQ(model.GetEntry(0)->title, "Log");
    EXPECT_EQ(model.GetEntry(1)->title, "Console");

    EXPECT_EQ(model.IndexOf("console"), std::optional<std::size_t>{1u});
    EXPECT_EQ(model.IndexOf(kUnknownId), std::nullopt);
    EXPECT_EQ(model.FindEntry(kUnknownId), nullptr);
    EXPECT_NE(model.FindEntry(kToolRowLogId), nullptr);
}

TEST(EditorToolRowModelTest, DuplicateIdIsRejected)
{
    EditorToolRowModel model;
    const std::size_t first = model.AddEntry(kToolRowLogId, "Log", true);
    const std::size_t second = model.AddEntry(kToolRowLogId, "Log Again", true);

    EXPECT_EQ(first, second);
    EXPECT_EQ(model.GetEntryCount(), 1u);
    // The original title survives; the duplicate never becomes a second tab.
    EXPECT_EQ(model.GetEntry(0)->title, "Log");
}

TEST(EditorToolRowModelTest, FirstOpenDockedEntryBecomesActive)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);
    model.AddEntry(kToolRowConsoleId, "Console", false);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, ClosedEntryNeverBecomesActive)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", false);
    model.AddEntry(kToolRowConsoleId, "Console", true);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});
    EXPECT_FALSE(model.IsOpen(0));
}

TEST(EditorToolRowModelTest, ClosingTheActiveTabSelectsTheNextOpenEntry)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);
    model.SetActiveIndex(1);

    model.SetOpen(1, false);

    // Forward from the closed index first.
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{2u});
}

TEST(EditorToolRowModelTest, ClosingTheLastEntryFallsBackToOneStepLeftAndThenClears)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);
    model.SetActiveIndex(2);

    // Nothing to the right, so the search falls back leftwards one step at a time:
    // the adjacent neighbour, not the leftmost entry.
    model.SetOpen(2, false);
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});

    model.SetOpen(1, false);
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});

    model.SetOpen(0, false);
    EXPECT_EQ(model.GetActiveIndex(), std::nullopt);
    EXPECT_FALSE(model.HasVisibleDockedPanel());
}

TEST(EditorToolRowModelTest, ClosingAnInactiveTabLeavesActiveAlone)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);
    model.SetActiveIndex(0);

    model.SetOpen(2, false);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, IsolatingTheActiveTabMovesActiveToTheNextDockedEntry)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);

    model.SetDocked(0, false);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});
    EXPECT_EQ(Detached(model), (std::vector<std::size_t>{0u}));
    EXPECT_TRUE(model.HasVisibleDockedPanel());
    // An isolated entry is still open: it is on screen, just not in the row.
    EXPECT_TRUE(model.IsOpen(0));
}

TEST(EditorToolRowModelTest, RedockingAnOpenTabMakesItActive)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.SetDocked(0, false);
    ASSERT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});

    model.SetDocked(0, true);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
    EXPECT_TRUE(Detached(model).empty());
}

TEST(EditorToolRowModelTest, RedockingAClosedTabDoesNotMakeItActive)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.SetOpen(0, false);
    model.SetDocked(0, false);

    model.SetDocked(0, true);

    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});
    EXPECT_FALSE(model.IsOpen(0));
}

TEST(EditorToolRowModelTest, MenuAndTabShareOneState)
{
    // The requirement this pins: a View-menu toggle and the tab strip's indexed
    // view must never disagree, because they are the same value.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);
    const std::optional<std::size_t> index = model.IndexOf(kToolRowLogId);
    ASSERT_TRUE(index.has_value());

    model.ToggleOpenById(kToolRowLogId);
    EXPECT_FALSE(model.IsOpenById(kToolRowLogId));
    EXPECT_FALSE(model.IsOpen(*index));

    model.ToggleOpenById(kToolRowLogId);
    EXPECT_TRUE(model.IsOpenById(kToolRowLogId));
    EXPECT_TRUE(model.IsOpen(*index));
}

TEST(EditorToolRowModelTest, UnknownIdIsInert)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    EXPECT_FALSE(model.IsOpenById(kUnknownId));
    model.ToggleOpenById(kUnknownId);

    EXPECT_EQ(model.GetEntryCount(), 1u);
    EXPECT_TRUE(model.IsOpenById(kToolRowLogId));
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, OutOfRangeIndicesAreSafe)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    EXPECT_EQ(model.GetEntry(99), nullptr);
    EXPECT_FALSE(model.IsOpen(99));
    EXPECT_FALSE(model.IsDocked(99));

    model.SetOpen(99, false);
    model.SetDocked(99, true);
    model.SetActiveIndex(99);
    model.ToggleOpen(99);

    EXPECT_EQ(model.GetEntryCount(), 1u);
    EXPECT_TRUE(model.IsOpen(0));
    EXPECT_TRUE(model.IsDocked(0));
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, VisibilityAndDockChangesNeverRemoveAnEntry)
{
    // Dock state is never destructive: an entry can leave the strip and come back, but it
    // is never removed, so the View menu always has something to act on.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", false);

    model.SetOpen(0, false);
    model.SetOpen(0, true);
    model.SetDocked(1, false);
    model.SetDocked(1, true);
    model.ToggleOpenById("a");
    model.SetDocked(0, false);

    EXPECT_EQ(model.GetEntryCount(), 3u);
}

TEST(EditorToolRowModelTest, AnIsolatedEntryLeavesTheStripEntirely)
{
    // The reported bug: an isolated panel kept drawing a dimmed tab, so two surfaces
    // claimed to be the same panel. The strip is now exactly the open, docked entries.
    EditorToolRowModel model;
    model.AddEntry("log", "Log", true);
    model.AddEntry("console", "Console", true);
    EXPECT_EQ(model.GetStripIndices(), (std::vector<std::size_t>{0u, 1u}));

    model.SetDocked(0, false);

    EXPECT_EQ(model.GetStripIndices(), (std::vector<std::size_t>{1u}))
        << "an isolated panel must not draw a tab";
    EXPECT_EQ(model.GetDetachedIndices(), (std::vector<std::size_t>{0u}))
        << "it is on screen as its own window instead";
    EXPECT_TRUE(model.IsOpen(0)) << "isolating is not hiding";
}

TEST(EditorToolRowModelTest, ShowInRowDocksAnIsolatedPanelAndMakesItActive)
{
    // The other reported bug: View > Log could not bring an isolated panel back, because
    // toggling visibility left it floating.
    EditorToolRowModel model;
    model.AddEntry("log", "Log", true);
    model.AddEntry("console", "Console", true);
    model.SetDocked(0, false);
    ASSERT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{1u});

    EXPECT_TRUE(model.ShowInRowById("log"));

    EXPECT_TRUE(model.IsDocked(0));
    EXPECT_TRUE(model.IsOpen(0));
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u})
        << "showing a panel should select it, not silently dock it behind another tab";
    EXPECT_TRUE(model.GetDetachedIndices().empty());
    EXPECT_EQ(model.GetStripIndices(), (std::vector<std::size_t>{0u, 1u}));
}

TEST(EditorToolRowModelTest, ShowInRowReopensAClosedPanelAndDocksIt)
{
    // Both flags have to be set: SetDocked only promotes an already-open entry, so docking
    // before opening would leave the panel docked but not drawn.
    EditorToolRowModel model;
    model.AddEntry("log", "Log", true);
    model.SetOpen(0, false);
    model.SetDocked(0, false);
    ASSERT_FALSE(model.IsOpen(0));

    EXPECT_TRUE(model.ShowInRowById("log"));

    EXPECT_TRUE(model.IsOpen(0));
    EXPECT_TRUE(model.IsDocked(0));
    EXPECT_EQ(model.GetActiveIndex(), std::optional<std::size_t>{0u});
    EXPECT_EQ(model.GetStripIndices(), (std::vector<std::size_t>{0u}));
}

TEST(EditorToolRowModelTest, ShowInRowIsInertForAnUnknownId)
{
    EditorToolRowModel model;
    model.AddEntry("log", "Log", true);

    EXPECT_FALSE(model.ShowInRowById("nope"));
    EXPECT_EQ(model.GetEntryCount(), 1u);
    EXPECT_EQ(model.GetStripIndices(), (std::vector<std::size_t>{0u}));
}

TEST(EditorToolRowModelTest, ActiveIsAlwaysOpenAndDocked)
{
    // The model's core invariant, checked after every mutation in a mixed sweep.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);

    const auto check_invariant = [&model]
    {
        const std::optional<std::size_t> active = model.GetActiveIndex();
        if (active.has_value())
        {
            EXPECT_TRUE(model.IsOpen(*active)) << "active index " << *active << " is closed";
            EXPECT_TRUE(model.IsDocked(*active)) << "active index " << *active << " is isolated";
        }
    };

    check_invariant();
    model.SetActiveIndex(1);
    check_invariant();
    model.SetDocked(1, false);
    check_invariant();
    model.SetOpen(0, false);
    check_invariant();
    model.ToggleOpenById("c");
    check_invariant();
    model.SetDocked(0, false);
    check_invariant();
    model.SetOpen(2, true);
    check_invariant();
}

TEST(EditorToolRowModelTest, EntryAddressesAreStableAcrossRegistration)
{
    // The row binds each panel to its entry's visibility pointer. Growing the
    // entry store must not move existing entries, or those pointers dangle.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    const EditorToolRowEntry *const first = model.FindEntry("a");
    const EditorWindowVisibility *const visibility = &first->visibility;
    ASSERT_NE(first, nullptr);

    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);
    model.AddEntry("d", "D", true);
    model.AddEntry("e", "E", true);
    model.AddEntry("f", "F", true);

    const EditorToolRowEntry *const again = model.FindEntry("a");
    EXPECT_EQ(first, again);
    EXPECT_EQ(visibility, &again->visibility);
    EXPECT_EQ(again, model.GetEntry(0));

    // Mutating through the model is observed through the borrowed pointer.
    model.SetOpen(0, false);
    EXPECT_FALSE(visibility->IsOpen());
}

TEST(EditorToolRowDropTest, InsideTheRowResolvesToTheTabStrip)
{
    const EditorRect row{0.0f, 700.0f, 1280.0f, 200.0f};

    EXPECT_EQ(ResolveToolRowDrop(row, 640.0f, 800.0f), EditorToolDropTarget::TabStrip);
    EXPECT_EQ(ResolveToolRowDrop(row, 0.0f, 700.0f), EditorToolDropTarget::TabStrip);
    EXPECT_EQ(ResolveToolRowDrop(row, 1280.0f, 900.0f), EditorToolDropTarget::TabStrip);
}

TEST(EditorToolRowDropTest, OutsideTheRowResolvesToFloat)
{
    const EditorRect row{0.0f, 700.0f, 1280.0f, 200.0f};

    // Above, below, left, right, and off-screen negative.
    EXPECT_EQ(ResolveToolRowDrop(row, 640.0f, 699.0f), EditorToolDropTarget::Float);
    EXPECT_EQ(ResolveToolRowDrop(row, 640.0f, 901.0f), EditorToolDropTarget::Float);
    EXPECT_EQ(ResolveToolRowDrop(row, -1.0f, 800.0f), EditorToolDropTarget::Float);
    EXPECT_EQ(ResolveToolRowDrop(row, 1281.0f, 800.0f), EditorToolDropTarget::Float);
    EXPECT_EQ(ResolveToolRowDrop(row, -50.0f, -50.0f), EditorToolDropTarget::Float);
}

TEST(EditorToolRowDropTest, DegenerateRowRectResolvesToNone)
{
    // An unlaid-out or collapsed row must not swallow a drop as "dock".
    const EditorRect zero_height{0.0f, 700.0f, 1280.0f, 0.0f};
    const EditorRect zero_width{0.0f, 700.0f, 0.0f, 200.0f};
    const EditorRect inverted{0.0f, 700.0f, -10.0f, -10.0f};

    EXPECT_EQ(ResolveToolRowDrop(zero_height, 640.0f, 800.0f), EditorToolDropTarget::None);
    EXPECT_EQ(ResolveToolRowDrop(zero_width, 640.0f, 800.0f), EditorToolDropTarget::None);
    EXPECT_EQ(ResolveToolRowDrop(inverted, 640.0f, 800.0f), EditorToolDropTarget::None);
}
