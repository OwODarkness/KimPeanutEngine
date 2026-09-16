#include <gtest/gtest.h>

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_tool_row_model.h"

// The placement model is ImGui-free by design, so this target links only KP::GoogleTest.
// Everything it owns — which dock a panel is in, which of a dock's panels is on top,
// visibility, the lock, and the drop rule — is covered here; the widget behaviour that
// needs a frame is covered by editor smoke instead.
namespace
{
    using kpengine::editor::EditorLayoutModel;
    using kpengine::editor::EditorLayoutSlot;
    using kpengine::editor::EditorPlacementTarget;
    using kpengine::editor::EditorPlacementTargetKind;
    using kpengine::editor::EditorRect;
    using kpengine::editor::EditorToolRowEntry;
    using kpengine::editor::EditorToolRowModel;
    using kpengine::editor::EditorWindowVisibility;
    using kpengine::editor::kToolRowConsoleId;
    using kpengine::editor::kToolRowLogId;
    using kpengine::editor::kToolRowViewportId;
    using kpengine::editor::ResolvePlacementDrop;

    constexpr const char *kUnknownId = "nope";

    // The default tree against a normal work area, which is what a drag actually resolves
    // against. Every region except the status bar is a dock.
    EditorLayoutModel ResolvedLayout()
    {
        EditorLayoutModel layout;
        layout.Resolve(EditorRect{0.0f, 0.0f, 1280.0f, 720.0f});
        return layout;
    }

    // A point, carried in a rect because that is what the drop rule's callers have.
    EditorRect Point(float x, float y)
    {
        return EditorRect{x, y, 0.0f, 0.0f};
    }

    EditorRect CentreOf(const EditorRect &rect)
    {
        return Point(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f);
    }

    EditorPlacementTarget DropAt(const EditorLayoutModel &layout, const EditorRect &point)
    {
        return ResolvePlacementDrop(layout, point.x, point.y);
    }

    std::vector<std::string> IdsOf(const EditorToolRowModel &model,
                                   const std::vector<std::size_t> &indices)
    {
        std::vector<std::string> ids;
        for (const std::size_t index : indices)
        {
            const EditorToolRowEntry *const entry = model.GetEntry(index);
            ids.push_back(entry != nullptr ? entry->id : std::string{"?"});
        }
        return ids;
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
    // The original title survives; the duplicate never becomes a second panel.
    EXPECT_EQ(model.GetEntry(0)->title, "Log");
}

TEST(EditorToolRowModelTest, AnEntryStartsInTheBottomStripByDefault)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    // The bottom strip is where every panel started before docks existed, so it is the
    // default that keeps registration a one-liner.
    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
}

TEST(EditorToolRowModelTest, StartingInSomethingThatIsNotADockFallsBackToTheStrip)
{
    // A bad dock is a wiring mistake. Falling back keeps registration total, which is
    // better than a panel that exists but is drawn nowhere.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ProfileBar);
    model.AddEntry(kToolRowConsoleId, "Console", true, EditorLayoutSlot::Count);

    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
    EXPECT_EQ(model.GetDock(1), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
}

TEST(EditorToolRowModelTest, EntryAddressesAreStableAcrossRegistration)
{
    // The host binds each panel to its entry's visibility pointer. Growing the entry store
    // must not move existing entries, or those pointers dangle.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true);
    const EditorToolRowEntry *const first = model.FindEntry("a");
    ASSERT_NE(first, nullptr);
    const EditorWindowVisibility *const visibility = &first->visibility;

    model.AddEntry("b", "B", true);
    model.AddEntry("c", "C", true);
    model.AddEntry("d", "D", true);
    model.AddEntry("e", "E", true);
    model.AddEntry("f", "F", true);

    const EditorToolRowEntry *const again = model.FindEntry("a");
    EXPECT_EQ(first, again);
    EXPECT_EQ(visibility, &again->visibility);

    model.SetOpen(0, false);
    EXPECT_FALSE(visibility->IsOpen());
}

TEST(EditorDockTest, ADocksMembersAreItsOpenEntriesInEntryOrder)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::ToolRow);
    model.AddEntry("c", "C", true, EditorLayoutSlot::Viewport);
    model.AddEntry("d", "D", false, EditorLayoutSlot::Viewport);

    EXPECT_EQ(IdsOf(model, model.GetDockMembers(EditorLayoutSlot::Viewport)),
              (std::vector<std::string>{"a", "c"}))
        << "a closed panel is not a member: it is not on screen to be shown";
    EXPECT_EQ(IdsOf(model, model.GetDockMembers(EditorLayoutSlot::ToolRow)),
              (std::vector<std::string>{"b"}));
    EXPECT_TRUE(model.GetDockMembers(EditorLayoutSlot::CameraSettings).empty());
}

TEST(EditorDockTest, TheFirstMemberOfADockIsItsActiveOne)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{0u});
}

TEST(EditorDockTest, EachDockHasItsOwnActiveMember)
{
    // The point of per-dock activity: the Viewport dock and the bottom strip are both on
    // screen at once, so one global "active panel" could only serve one of them.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);
    model.AddEntry("c", "C", true, EditorLayoutSlot::ToolRow);
    model.AddEntry("d", "D", true, EditorLayoutSlot::ToolRow);

    model.SetActiveInDock(EditorLayoutSlot::Viewport, 1);
    model.SetActiveInDock(EditorLayoutSlot::ToolRow, 3);

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{1u});
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::ToolRow), std::optional<std::size_t>{3u});
}

TEST(EditorDockTest, AClosedMemberIsNeverActive)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", false, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{1u});
    EXPECT_FALSE(model.IsOpen(0));
}

TEST(EditorDockTest, SettingActiveToANonMemberIsIgnored)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::ToolRow);

    // b is not in the Viewport dock, so it cannot be its active member.
    model.SetActiveInDock(EditorLayoutSlot::Viewport, 1);

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{0u});
}

TEST(EditorDockTest, ClosingTheActiveMemberSelectsTheNextOneInThatDock)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);
    model.AddEntry("c", "C", true, EditorLayoutSlot::Viewport);
    model.SetActiveInDock(EditorLayoutSlot::Viewport, 1);

    model.SetOpen(1, false);

    // Forward from the closed index first.
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{2u});
}

TEST(EditorDockTest, ClosingTheLastMemberFallsBackOneStepLeftAndThenClears)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);
    model.AddEntry("c", "C", true, EditorLayoutSlot::Viewport);
    model.SetActiveInDock(EditorLayoutSlot::Viewport, 2);

    // Nothing to the right, so the search falls back leftwards one step at a time: the
    // adjacent neighbour, not the leftmost entry.
    model.SetOpen(2, false);
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{1u});

    model.SetOpen(1, false);
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{0u});

    model.SetOpen(0, false);
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::nullopt);
    EXPECT_TRUE(model.GetDockMembers(EditorLayoutSlot::Viewport).empty());
    EXPECT_FALSE(model.IsDockOccupied(EditorLayoutSlot::Viewport));
}

TEST(EditorDockTest, ClosingAMemberDoesNotReachIntoAnotherDock)
{
    // The neighbour search is scoped to the dock. Without that, closing the only Viewport
    // member would select a panel living somewhere else entirely.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::ToolRow);

    model.SetOpen(0, false);

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::nullopt);
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::ToolRow), std::optional<std::size_t>{1u});
}

TEST(EditorDockTest, MovingIntoADockJoinsItRatherThanReplacingItsMembers)
{
    // The rule that replaced ED3's "a drop never displaces a panel": a non-empty dock takes
    // the arrival as another tab, and nobody is evicted.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    model.AddEntry(kToolRowViewportId, "Viewport", true, EditorLayoutSlot::Viewport);

    ASSERT_TRUE(model.MoveToDockById(kToolRowLogId, EditorLayoutSlot::Viewport));

    EXPECT_EQ(IdsOf(model, model.GetDockMembers(EditorLayoutSlot::Viewport)),
              (std::vector<std::string>{"log", "viewport"}));
    EXPECT_TRUE(model.GetDockMembers(EditorLayoutSlot::ToolRow).empty())
        << "the panel left its old dock";
    EXPECT_TRUE(model.IsDockOccupied(EditorLayoutSlot::Viewport));
    EXPECT_FALSE(model.IsDockOccupied(EditorLayoutSlot::ToolRow));
}

TEST(EditorDockTest, TheMovedPanelComesForwardInItsNewDock)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);
    ASSERT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport),
              std::optional<std::size_t>{0u});

    ASSERT_TRUE(model.MoveToDock(1, EditorLayoutSlot::ToolRow));
    ASSERT_TRUE(model.MoveToDock(1, EditorLayoutSlot::Viewport));

    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport), std::optional<std::size_t>{1u})
        << "a drop that left the panel hidden behind another tab would read as broken";
}

TEST(EditorDockTest, MovingOpensAClosedPanel)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", false, EditorLayoutSlot::Viewport);
    ASSERT_FALSE(model.IsOpen(0));

    ASSERT_TRUE(model.MoveToDock(0, EditorLayoutSlot::ToolRow));

    EXPECT_TRUE(model.IsOpen(0)) << "a drag implies a panel you can see";
    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
}

TEST(EditorDockTest, MovingToSomethingThatIsNotADockIsRefused)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);

    EXPECT_FALSE(model.MoveToDock(0, EditorLayoutSlot::ProfileBar));
    EXPECT_FALSE(model.MoveToDock(0, EditorLayoutSlot::Count));

    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::Viewport});
}

TEST(EditorDockTest, MovingIsInertForAnUnknownIdOrAStaleIndex)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);

    EXPECT_FALSE(model.MoveToDockById(kUnknownId, EditorLayoutSlot::ToolRow));
    EXPECT_FALSE(model.MoveToDock(99, EditorLayoutSlot::ToolRow));

    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::Viewport});
    EXPECT_EQ(model.GetEntryCount(), 1u);
}

TEST(EditorDockTest, FloatingTakesAPanelOutOfEveryDock)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);

    ASSERT_TRUE(model.FloatPanel(0));

    EXPECT_FALSE(model.GetDock(0).has_value());
    EXPECT_TRUE(model.IsOpen(0)) << "floating is not hiding";
    EXPECT_TRUE(model.GetFloatingIndices() == std::vector<std::size_t>{0u});
    EXPECT_TRUE(model.GetDockMembers(EditorLayoutSlot::ToolRow).empty());
    EXPECT_FALSE(model.HasVisibleDockedPanel());
}

TEST(EditorDockTest, FloatingOpensAClosedPanel)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", false, EditorLayoutSlot::ToolRow);

    ASSERT_TRUE(model.FloatPanel(0));

    // Otherwise "move this window" would make a window nobody could see.
    EXPECT_TRUE(model.IsOpen(0));
}

TEST(EditorDockTest, FloatingAnAlreadyFloatingPanelChangesNothing)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    ASSERT_TRUE(model.FloatPanel(0));
    const std::uint64_t revision = model.GetPlacementRevision();

    EXPECT_FALSE(model.FloatPanel(0)) << "already where it was asked to go";

    EXPECT_EQ(model.GetPlacementRevision(), revision);
}

TEST(EditorDockTest, AFloatingPanelIsNotAMemberOfAnyDock)
{
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::ToolRow);

    ASSERT_TRUE(model.FloatPanel(1));

    for (std::size_t slot = 0; slot < static_cast<std::size_t>(EditorLayoutSlot::Count); ++slot)
    {
        // Bound once: GetDockMembers returns by value, so iterating two temporaries would
        // compare a begin() and an end() from different vectors.
        const std::vector<std::size_t> members = model.GetDockMembers(
            static_cast<EditorLayoutSlot>(slot));
        EXPECT_EQ(std::find(members.begin(), members.end(), 1u), members.end())
            << "floating panel appears in dock " << slot;
    }
    EXPECT_EQ(model.GetFloatingIndices(), (std::vector<std::size_t>{1u}));
}

TEST(EditorToolRowModelTest, MenuAndDockShareOneState)
{
    // The requirement this pins: a View-menu toggle and the dock's own view must never
    // disagree, because they are the same value.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    model.ToggleOpenById(kToolRowLogId);
    EXPECT_FALSE(model.IsOpenById(kToolRowLogId));
    EXPECT_FALSE(model.IsOpen(0));

    model.ToggleOpenById(kToolRowLogId);
    EXPECT_TRUE(model.IsOpenById(kToolRowLogId));
    EXPECT_TRUE(model.IsOpen(0));
}

TEST(EditorToolRowModelTest, ShowInRowBringsAPanelHomeFromAnywhereUnlocked)
{
    // The View menu is always available and must always work: a panel that cannot be
    // brought back is a panel the user has lost.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    ASSERT_FALSE(model.GetDockMembers(EditorLayoutSlot::ToolRow).size());

    EXPECT_TRUE(model.ShowInRowById(kToolRowLogId));

    EXPECT_EQ(model.GetDockById(kToolRowLogId),
              std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::ToolRow), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, ShowInRowReopensAClosedPanelAndBringsItToTheStrip)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", false, EditorLayoutSlot::Viewport);

    EXPECT_TRUE(model.ShowInRowById(kToolRowLogId));

    EXPECT_TRUE(model.IsOpen(0));
    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
}

TEST(EditorToolRowModelTest, ShowInRowIsInertForAnUnknownId)
{
    EditorToolRowModel model;
    model.AddEntry("log", "Log", true);

    EXPECT_FALSE(model.ShowInRowById("nope"));
    EXPECT_EQ(model.GetEntryCount(), 1u);
}

TEST(EditorToolRowModelTest, ShowOpensAndActivatesAtTheCurrentDock)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    model.AddEntry(kToolRowConsoleId, "Console", false, EditorLayoutSlot::Viewport);

    EXPECT_TRUE(model.ShowById(kToolRowConsoleId));
    EXPECT_TRUE(model.IsOpenById(kToolRowConsoleId));
    EXPECT_EQ(model.GetDockById(kToolRowConsoleId),
              std::optional<EditorLayoutSlot>{EditorLayoutSlot::Viewport});
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::Viewport),
              std::optional<std::size_t>{1u});
}

TEST(EditorToolRowModelTest, FocusRequestsTheFloatingWindowWithoutMovingIt)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    ASSERT_TRUE(model.FloatPanel(0));

    EXPECT_TRUE(model.FocusById(kToolRowLogId));
    EXPECT_TRUE(model.ConsumeFocusRequest(0));
    EXPECT_FALSE(model.ConsumeFocusRequest(0));
    EXPECT_EQ(model.GetDockById(kToolRowLogId), std::nullopt);
}

TEST(EditorToolRowModelTest, FocusDoesNotOpenAClosedPanel)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", false, EditorLayoutSlot::ToolRow);

    EXPECT_FALSE(model.FocusById(kToolRowLogId));
    EXPECT_FALSE(model.ConsumeFocusRequest(0));
}

TEST(EditorLockTest, DockLockBelongsToTheRowContainerNotToOneTab)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    model.AddEntry(kToolRowConsoleId, "Console", true, EditorLayoutSlot::Viewport);

    model.SetDockLocked(EditorLayoutSlot::Viewport, true);

    EXPECT_TRUE(model.IsDockLocked(EditorLayoutSlot::Viewport));
    EXPECT_TRUE(model.IsLocked(0));
    EXPECT_TRUE(model.IsLocked(1));
    EXPECT_FALSE(model.MoveToDock(1, EditorLayoutSlot::ToolRow));

    model.SetDockLocked(EditorLayoutSlot::Viewport, false);
    EXPECT_FALSE(model.IsLocked(0));
    EXPECT_FALSE(model.IsLocked(1));
    EXPECT_TRUE(model.MoveToDock(1, EditorLayoutSlot::ToolRow));
}

TEST(EditorLockTest, FloatingPanelsKeepIndependentWindowLocks)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    ASSERT_TRUE(model.FloatPanel(0));

    model.SetLocked(0, true);
    EXPECT_TRUE(model.IsLocked(0));
    EXPECT_FALSE(model.IsDockLocked(EditorLayoutSlot::Viewport));
    EXPECT_FALSE(model.FloatPanel(0));
}

TEST(EditorLockTest, ALockedPanelRefusesToBeMovedOrFloated)
{
    // The lock's whole job. It gates starting a drag and nothing else, so these two are
    // the only places it can be observed.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    model.SetLocked(0, true);
    ASSERT_TRUE(model.IsLocked(0));

    EXPECT_FALSE(model.MoveToDock(0, EditorLayoutSlot::Viewport));
    EXPECT_FALSE(model.MoveToDockById(kToolRowLogId, EditorLayoutSlot::Viewport));
    EXPECT_FALSE(model.FloatPanel(0));

    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
}

TEST(EditorLockTest, UnlockingAllowsTheMoveAgain)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    model.SetLocked(0, true);
    ASSERT_FALSE(model.MoveToDock(0, EditorLayoutSlot::Viewport));

    model.SetLocked(0, false);

    EXPECT_TRUE(model.MoveToDock(0, EditorLayoutSlot::Viewport));
    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::Viewport});
}

TEST(EditorLockTest, TogglingByIdFlipsTheLock)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);
    ASSERT_FALSE(model.IsLockedById(kToolRowLogId));

    model.ToggleLockedById(kToolRowLogId);
    EXPECT_TRUE(model.IsLockedById(kToolRowLogId));

    model.ToggleLockedById(kToolRowLogId);
    EXPECT_FALSE(model.IsLockedById(kToolRowLogId));

    model.ToggleLockedById(kUnknownId);
    EXPECT_EQ(model.GetEntryCount(), 1u);
}

TEST(EditorLockTest, ShowInRowIgnoresTheLockBecauseItIsNotADrag)
{
    // Otherwise locking a panel while it floats would lose it: nothing else can bring it
    // home, and the lock would be a way to destroy a panel by accident.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    model.SetLockedById(kToolRowLogId, true);
    ASSERT_FALSE(model.MoveToDock(0, EditorLayoutSlot::ToolRow));

    EXPECT_TRUE(model.ShowInRowById(kToolRowLogId));

    EXPECT_EQ(model.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow});
    EXPECT_TRUE(model.IsLocked(0)) << "and it stays locked";
}

TEST(EditorLockTest, OutOfRangeAndUnknownLocksAreSafe)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    EXPECT_FALSE(model.IsLocked(99));
    EXPECT_FALSE(model.IsLockedById(kUnknownId));
    model.SetLocked(99, true);
    model.SetLockedById(kUnknownId, true);

    EXPECT_FALSE(model.IsLocked(0));
    EXPECT_EQ(model.GetEntryCount(), 1u);
}

TEST(EditorToolRowModelTest, TheRevisionBumpsOnPlacementAndLockChangesOnly)
{
    // This decides whether the layout file is rewritten, so a false positive is a write on
    // every cancelled drag and a false negative loses the arrangement.
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    model.AddEntry(kToolRowConsoleId, "Console", false, EditorLayoutSlot::ToolRow);
    const std::uint64_t baseline = model.GetPlacementRevision();

    model.ToggleOpenById(kToolRowConsoleId);
    model.ToggleOpenById(kToolRowConsoleId);
    EXPECT_EQ(model.GetPlacementRevision(), baseline)
        << "showing or hiding a panel does not change where anything is";

    model.SetActiveInDock(EditorLayoutSlot::ToolRow, 0);
    EXPECT_EQ(model.GetPlacementRevision(), baseline) << "nor does which tab is on top";

    model.SetLocked(0, true);
    EXPECT_NE(model.GetPlacementRevision(), baseline) << "the lock is persisted";

    const std::uint64_t locked = model.GetPlacementRevision();
    model.SetLocked(0, true);
    EXPECT_EQ(model.GetPlacementRevision(), locked) << "a redundant lock is not a change";

    // Unlocked again, or the lock would refuse the moves below and the assertions after
    // them would pass for the wrong reason.
    model.SetLocked(0, false);
    ASSERT_TRUE(model.MoveToDock(0, EditorLayoutSlot::Viewport));
    EXPECT_NE(model.GetPlacementRevision(), locked);

    const std::uint64_t moved = model.GetPlacementRevision();
    ASSERT_TRUE(model.MoveToDock(0, EditorLayoutSlot::Viewport));
    EXPECT_EQ(model.GetPlacementRevision(), moved) << "moving where it already is is not a move";

    ASSERT_TRUE(model.FloatPanel(0));
    EXPECT_NE(model.GetPlacementRevision(), moved);
}

TEST(EditorToolRowModelTest, ClearDropsEveryPlacementAndLock)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::Viewport);
    model.SetLocked(0, true);

    model.Clear();

    EXPECT_EQ(model.GetEntryCount(), 0u);
    EXPECT_FALSE(model.HasVisibleDockedPanel());
    EXPECT_TRUE(model.GetFloatingIndices().empty());
    for (std::size_t slot = 0; slot < static_cast<std::size_t>(EditorLayoutSlot::Count); ++slot)
    {
        EXPECT_FALSE(model.GetActiveInDock(static_cast<EditorLayoutSlot>(slot)).has_value());
    }
}

TEST(EditorToolRowModelTest, OutOfRangeIndicesAreSafe)
{
    EditorToolRowModel model;
    model.AddEntry(kToolRowLogId, "Log", true);

    EXPECT_EQ(model.GetEntry(99), nullptr);
    EXPECT_FALSE(model.IsOpen(99));
    EXPECT_FALSE(model.GetDock(99).has_value());

    model.SetOpen(99, false);
    model.SetActiveInDock(EditorLayoutSlot::ToolRow, 99);
    model.ToggleOpen(99);

    EXPECT_EQ(model.GetEntryCount(), 1u);
    EXPECT_TRUE(model.IsOpen(0));
    EXPECT_EQ(model.GetActiveInDock(EditorLayoutSlot::ToolRow), std::optional<std::size_t>{0u});
}

TEST(EditorToolRowModelTest, VisibilityAndPlacementChangesNeverRemoveAnEntry)
{
    // Placement is never destructive: a panel can leave a dock and come back, but it is
    // never removed, so the View menu always has something to act on.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::ToolRow);
    model.AddEntry("c", "C", false, EditorLayoutSlot::ToolRow);

    model.SetOpen(0, false);
    model.SetOpen(0, true);
    ASSERT_TRUE(model.MoveToDock(1, EditorLayoutSlot::Viewport));
    ASSERT_TRUE(model.FloatPanel(1));
    ASSERT_TRUE(model.MoveToDock(1, EditorLayoutSlot::ToolRow));
    model.ToggleOpenById("a");

    EXPECT_EQ(model.GetEntryCount(), 3u);
}

TEST(EditorToolRowModelTest, EveryDocksActiveMemberIsOneOfItsOwn)
{
    // The model's core invariant, checked after every mutation in a mixed sweep.
    EditorToolRowModel model;
    model.AddEntry("a", "A", true, EditorLayoutSlot::Viewport);
    model.AddEntry("b", "B", true, EditorLayoutSlot::Viewport);
    model.AddEntry("c", "C", true, EditorLayoutSlot::ToolRow);

    const auto check = [&model]
    {
        for (std::size_t slot = 0; slot < static_cast<std::size_t>(EditorLayoutSlot::Count); ++slot)
        {
            const auto dock = static_cast<EditorLayoutSlot>(slot);
            const std::optional<std::size_t> active = model.GetActiveInDock(dock);
            if (!active.has_value())
            {
                continue;
            }
            const EditorToolRowEntry *const entry = model.GetEntry(*active);
            EXPECT_NE(entry, nullptr);
            EXPECT_TRUE(entry->visibility.IsOpen()) << "dock " << slot << " shows a closed panel";
            EXPECT_EQ(entry->dock, std::optional<EditorLayoutSlot>{dock})
                << "dock " << slot << " shows a panel that lives elsewhere";
        }
    };

    check();
    model.SetActiveInDock(EditorLayoutSlot::Viewport, 1);
    check();
    ASSERT_TRUE(model.MoveToDock(1, EditorLayoutSlot::ToolRow));
    check();
    model.SetOpen(0, false);
    check();
    ASSERT_TRUE(model.FloatPanel(2));
    check();
    model.ToggleOpenById("a");
    check();
}

// The destination rule. Every gesture decision goes through this one function, so it is the
// highest-value thing in the stage to have covered.

TEST(EditorPlacementDropTest, ADockUnderTheCursorIsTheAnswer)
{
    const EditorLayoutModel layout = ResolvedLayout();

    for (const EditorLayoutSlot dock :
         {EditorLayoutSlot::Viewport, EditorLayoutSlot::WorldOutliner,
          EditorLayoutSlot::ActorInspector, EditorLayoutSlot::CameraSettings,
          EditorLayoutSlot::DebugViewer, EditorLayoutSlot::GpuProfiler,
          EditorLayoutSlot::ToolRow})
    {
        const EditorPlacementTarget target = DropAt(layout, CentreOf(layout.RectOf(dock)));

        EXPECT_EQ(target.kind, EditorPlacementTargetKind::Dock)
            << "dock " << static_cast<int>(dock);
        EXPECT_EQ(target.dock, dock);
    }
}

TEST(EditorPlacementDropTest, AnOccupiedDockIsStillADestination)
{
    // ED3 refused an occupied region; docks take the arrival as another tab, so the answer
    // no longer depends on who is already there.
    const EditorLayoutModel layout = ResolvedLayout();

    const EditorPlacementTarget target =
        DropAt(layout, CentreOf(layout.RectOf(EditorLayoutSlot::Viewport)));

    EXPECT_EQ(target.kind, EditorPlacementTargetKind::Dock);
    EXPECT_EQ(target.dock, EditorLayoutSlot::Viewport);
}

TEST(EditorPlacementDropTest, TheStatusBarIsNeverADestination)
{
    // A 43 px metrics strip is not somewhere a window goes.
    const EditorLayoutModel layout = ResolvedLayout();

    const EditorPlacementTarget target =
        DropAt(layout, CentreOf(layout.RectOf(EditorLayoutSlot::ProfileBar)));

    EXPECT_EQ(target.kind, EditorPlacementTargetKind::Float);
    EXPECT_EQ(target.dock, EditorLayoutSlot::Count);
}

TEST(EditorPlacementDropTest, OutsideEveryDockFloatsThePanel)
{
    const EditorLayoutModel layout = ResolvedLayout();

    // Above and left of the work area: outside the tiling entirely.
    EXPECT_EQ(DropAt(layout, Point(-40.0f, -40.0f)).kind, EditorPlacementTargetKind::Float);
    EXPECT_EQ(DropAt(layout, Point(5000.0f, 5000.0f)).kind, EditorPlacementTargetKind::Float);
}

TEST(EditorPlacementDropTest, AnUnresolvedLayoutAnswersNone)
{
    // Resolve was never called, so there are no rectangles on screen to aim at and a float
    // ghost would be a promise the release could not keep.
    const EditorLayoutModel layout;

    EXPECT_EQ(DropAt(layout, Point(0.0f, 0.0f)).kind, EditorPlacementTargetKind::None);
    EXPECT_EQ(DropAt(layout, Point(640.0f, 360.0f)).kind, EditorPlacementTargetKind::None);
}

TEST(EditorPlacementDropTest, AdjacentDocksPartitionThePlaneExactlyOnceEach)
{
    // Docks tile the work area, so a shared edge must belong to exactly one of them. An
    // inclusive test on both sides would let the earlier enum value claim every seam, and a
    // drop on the seam would land in the wrong dock.
    const EditorLayoutModel layout = ResolvedLayout();
    const EditorRect outliner = layout.RectOf(EditorLayoutSlot::WorldOutliner);
    const EditorRect inspector = layout.RectOf(EditorLayoutSlot::ActorInspector);

    ASSERT_FLOAT_EQ(outliner.y + outliner.height, inspector.y) << "the two are stacked";

    // The shared edge belongs to the lower dock, and to it alone.
    EXPECT_EQ(DropAt(layout, Point(inspector.x, inspector.y)).dock,
              EditorLayoutSlot::ActorInspector);
    EXPECT_EQ(DropAt(layout, Point(outliner.x, outliner.y + outliner.height)).dock,
              EditorLayoutSlot::ActorInspector);

    // The min edges are included, so the top-left corner of a dock is inside it.
    EXPECT_EQ(DropAt(layout, Point(outliner.x, outliner.y)).dock,
              EditorLayoutSlot::WorldOutliner);
}
