#ifndef KPENGINE_EDITOR_TOOL_ROW_MODEL_H
#define KPENGINE_EDITOR_TOOL_ROW_MODEL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_layout_rect.h"
#include "editor/ui/component/editor_window_visibility.h"

// This header must stay ImGui-free. The tool row's tab, visibility, and
// dock/isolate/placement rules live here so they are testable without a frame, and the
// unit-test target deliberately links no ImGui. Including editor_window_component.h
// here would pull in imgui.h and break that layering.
//
// editor_layout_model.h is included for EditorLayoutSlot and for the resolved region
// rectangles the drop rule needs. It is ImGui-free too, and it does not include this
// header, so there is no cycle.

namespace kpengine::editor
{
    // Stable ids. The View menu binds by these and a persisted placement names a panel by
    // one, so they must never be renamed or localized; the titles are the user-facing
    // strings. Every workspace panel has one, because every one of them is an entry.
    inline constexpr const char *kToolRowViewportId = "viewport";
    inline constexpr const char *kToolRowWorldOutlinerId = "world_outliner";
    inline constexpr const char *kToolRowActorInspectorId = "actor_inspector";
    inline constexpr const char *kToolRowCameraSettingsId = "camera_settings";
    inline constexpr const char *kToolRowDebugViewerId = "debug_viewer";
    inline constexpr const char *kToolRowGpuProfilerId = "gpu_profiler";
    inline constexpr const char *kToolRowLogId = "log";
    inline constexpr const char *kToolRowConsoleId = "console";
    inline constexpr const char *kToolRowAssetBrowserId = "asset_browser";

    // EditorRect lives in editor_layout_rect.h, shared with the layout model so the
    // editor has one screen-space rectangle type rather than two.

    enum class EditorToolDropTarget
    {
        None,      // degenerate row rect; the row is not laid out this frame
        TabStrip,  // drop back into the tool row
        Float,     // drop anywhere else: become a standalone window
    };

    // Decides what a drag release at (x, y) means. Bounds are inclusive on both
    // edges, and a degenerate rect resolves to None rather than guessing.
    //
    // This is the row-versus-float rule only. The full placement gesture uses
    // ResolvePlacementDrop below, which also considers the workspace regions.
    EditorToolDropTarget ResolveToolRowDrop(const EditorRect &row, float x, float y) noexcept;

    enum class EditorPlacementTargetKind
    {
        None,  // nothing is under the cursor and no dock is laid out
        Dock,  // this dock: `dock` names it
        Float, // a standalone window wherever ImGui puts it
    };

    struct EditorPlacementTarget
    {
        EditorPlacementTargetKind kind = EditorPlacementTargetKind::None;
        EditorLayoutSlot dock = EditorLayoutSlot::Count;  // valid only when kind == Dock
    };

    // One panel. Every workspace panel is one of these, wherever it is drawn, which is what
    // makes moving a panel between docks a pure assignment rather than an ownership change.
    struct EditorToolRowEntry
    {
        std::string id;                     // stable, unique; also the ImGui id source
        std::string title;                  // tab label and window title
        EditorWindowVisibility visibility;  // ONE shared open state
        // A floating panel owns its lock. Once docked, the container owns the lock and this
        // value is ignored; keeping it here preserves standalone-window state while a panel
        // moves between floating and docked layouts.
        bool locked = false;
        // Where the panel lives. ToolRow is the bottom strip; any other slot is that dock's
        // window, shared with its other members as tabs; nullopt floats it as its own window
        // wherever ImGui puts it.
        //
        // One field, not the pair ED3 carried ("docked" plus "region"), because the tool row
        // IS a region — so "in the strip" is just dock == ToolRow, and the two facts that
        // could disagree are now one.
        std::optional<EditorLayoutSlot> dock;
    };

    // Every workspace panel: where it is, whether it is open, and whether it may be moved.
    // One model holds all of them, wherever they are drawn, which is what makes a move
    // between docks an assignment rather than a transfer of ownership.
    //
    // Entries are stored in a deque, not a vector: the host hands each panel a pointer to
    // its entry's visibility for the panel's whole lifetime, and a vector reallocation on
    // AddEntry would dangle it.
    class EditorToolRowModel final
    {
    public:
        // Appends a panel. A duplicate id is rejected and its existing index returned, so
        // registration can never produce two entries with one id. The default dock is the
        // bottom strip, which is where every panel started before docks existed.
        std::size_t AddEntry(std::string id, std::string title, bool open = true,
                             EditorLayoutSlot dock = EditorLayoutSlot::ToolRow);

        // Drops every entry. Used when the workspace is rebuilt, so the model cannot
        // inherit stale visibility or placement from a torn-down tree.
        void Clear() noexcept;

        std::size_t GetEntryCount() const noexcept;
        const EditorToolRowEntry *GetEntry(std::size_t index) const noexcept;
        const EditorToolRowEntry *FindEntry(std::string_view id) const noexcept;
        std::optional<std::size_t> IndexOf(std::string_view id) const noexcept;

        // Non-const entry access, for binding a panel to its entry's visibility.
        EditorToolRowEntry *GetEntryMutable(std::size_t index) noexcept;

        bool IsOpen(std::size_t index) const noexcept;
        void SetOpen(std::size_t index, bool open);
        void ToggleOpen(std::size_t index);

        bool IsOpenById(std::string_view id) const noexcept;
        void ToggleOpenById(std::string_view id);
        void SetOpenById(std::string_view id, bool open);

        // Makes a panel visible IN THE BOTTOM STRIP, docking it there if it was floating or
        // in another dock. "Show this panel" has to mean "put it back in the row": toggling
        // visibility alone leaves a floating panel floating, so a View menu built on
        // visibility could never bring one home.
        bool ShowInRowById(std::string_view id);

        // --- Locks -------------------------------------------------------------------
        //
        // A dock is a row container. Its lock is shown in the container chrome and gates
        // dragging every tab in that container. Floating panels retain an individual lock.
        bool IsDockLocked(EditorLayoutSlot dock) const noexcept;
        void SetDockLocked(EditorLayoutSlot dock, bool locked);
        void ToggleDockLocked(EditorLayoutSlot dock);

        // Compatibility-shaped panel queries: for a docked panel these query or change the
        // containing row lock; for a floating panel they query or change its own lock.
        bool IsLocked(std::size_t index) const noexcept;
        bool IsLockedById(std::string_view id) const noexcept;
        void SetLocked(std::size_t index, bool locked);
        void SetLockedById(std::string_view id, bool locked);
        void ToggleLockedById(std::string_view id);

        // --- Docks ------------------------------------------------------------------
        //
        // A panel lives in exactly one dock, or in none and floats. A dock draws one of its
        // members, or tabs when it has several. See EditorLayoutModel::IsDock for which
        // regions are docks.

        std::optional<EditorLayoutSlot> GetDock(std::size_t index) const noexcept;
        std::optional<EditorLayoutSlot> GetDockById(std::string_view id) const noexcept;

        // The panels drawn in this dock, in entry order: every OPEN entry whose dock is
        // that slot. Two or more of them are drawn as tabs.
        std::vector<std::size_t> GetDockMembers(EditorLayoutSlot dock) const;
        bool IsDockOccupied(EditorLayoutSlot dock) const noexcept;
        // True when any dock has at least one member.
        bool HasVisibleDockedPanel() const noexcept;

        // Panels with no dock: each is its own window, wherever ImGui puts it.
        std::vector<std::size_t> GetFloatingIndices() const;

        // Moves a panel into a dock, opening it. Refuses an out-of-range index, an unknown
        // id, a slot that is not a dock, and a locked source panel/container.
        //
        // Never displaces: a dock's existing members stay, and the moved panel joins them.
        // That is what lets a dock hold several panels, and it is why nothing has to be
        // evicted when a panel arrives.
        bool MoveToDock(std::size_t index, EditorLayoutSlot dock);
        bool MoveToDockById(std::string_view id, EditorLayoutSlot dock);

        // Moves a panel out of every dock so it stands alone. Refuses a locked source
        // panel/container.
        bool FloatPanel(std::size_t index);

        // Which member of this dock is on top. Per dock, not global: every dock shows one
        // of its own members at the same time, so one "active index" cannot serve them all.
        std::optional<std::size_t> GetActiveInDock(EditorLayoutSlot dock) const noexcept;
        void SetActiveInDock(EditorLayoutSlot dock, std::size_t index);

        // Bumped by every change to where an entry is drawn, or to its lock, so the editor
        // can persist on change rather than per frame. Visibility toggles do not bump it.
        std::uint64_t GetPlacementRevision() const noexcept { return placement_revision_; }

        // Restores the invariant that a dock's active member is one of its members. Every
        // mutator ends here, so no caller can leave a dock pointing at a panel that left.
        void ReconcileActive();

    private:
        // The move itself, without the lock gate. ShowInRowById is the one caller allowed
        // past it: an explicit menu action is recovery, not dragging.
        bool MoveToDockUnchecked(std::size_t index, EditorLayoutSlot dock);
        void BumpPlacementRevision() noexcept { ++placement_revision_; }

        std::deque<EditorToolRowEntry> entries_;
        std::array<bool, kEditorLayoutSlotCount> dock_locked_{};
        // One active member per dock. An entry can only be in one dock, so at most one of
        // these can name any given index.
        std::array<std::optional<std::size_t>, kEditorLayoutSlotCount> active_{};
        std::uint64_t placement_revision_ = 0;
    };

    // Decides what a drag release at (x, y) means, over the whole workspace. A dock under
    // the cursor wins; otherwise the drop floats. A release over nowhere with no dock
    // resolved answers None.
    //
    // There is no "occupied" case any more: a dock takes a panel whether or not it already
    // holds one, joining its members. That is what ED3's "a drop never displaces a panel"
    // rule was working around, and it is gone.
    EditorPlacementTarget ResolvePlacementDrop(const EditorLayoutModel &layout, float x,
                                               float y) noexcept;
}

#endif // KPENGINE_EDITOR_TOOL_ROW_MODEL_H
