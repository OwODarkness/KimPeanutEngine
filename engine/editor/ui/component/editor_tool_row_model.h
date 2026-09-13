#ifndef KPENGINE_EDITOR_TOOL_ROW_MODEL_H
#define KPENGINE_EDITOR_TOOL_ROW_MODEL_H

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor/ui/component/editor_layout_rect.h"
#include "editor/ui/component/editor_window_visibility.h"

// This header must stay ImGui-free. The tool row's tab, visibility, and
// dock/isolate rules live here so they are testable without a frame, and the
// unit-test target deliberately links no ImGui. Including editor_window_component.h
// here would pull in imgui.h and break that layering.

namespace kpengine::editor
{
    // Stable ids. The View menu binds by these, so they must never be renamed or
    // localized; the tab titles are the user-facing strings.
    inline constexpr const char *kToolRowLogId = "log";
    inline constexpr const char *kToolRowConsoleId = "console";

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
    EditorToolDropTarget ResolveToolRowDrop(const EditorRect &row, float x, float y) noexcept;

    struct EditorToolRowEntry
    {
        std::string id;                     // stable, unique; also the ImGui id source
        std::string title;                  // tab label and detached-window title
        EditorWindowVisibility visibility;  // ONE shared open state
        bool docked = true;                 // false = isolated into its own window
    };

    // Tab order, active tab, and per-panel visibility/dock state for the bottom
    // tool row. The component is a thin translation of this state.
    //
    // Entries are stored in a deque, not a vector: the row hands each panel a
    // pointer to its entry's visibility for the panel's whole lifetime, and a
    // vector reallocation on AddEntry would dangle it.
    class EditorToolRowModel final
    {
    public:
        // Appends a panel. A duplicate id is rejected and its existing index
        // returned, so registration can never produce two entries with one id.
        std::size_t AddEntry(std::string id, std::string title,
                             bool open = true, bool docked = true);

        // Drops every entry. Used when the workspace is rebuilt, so the row cannot
        // inherit stale visibility or dock state from a torn-down tree.
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

        // Makes an entry visible IN THE ROW, docking it if it was isolated. "Show this
        // panel" has to mean "put it back in the row": toggling visibility alone leaves an
        // isolated panel floating, so a View menu built on visibility can never dock one
        // and the panel looks impossible to bring back.
        bool ShowInRowById(std::string_view id);

        // The tabs the strip draws: open AND docked, in entry order. An isolated entry is
        // deliberately absent — it is on screen as its own window, and drawing it in the
        // strip as well would leave two surfaces claiming to be the same panel.
        std::vector<std::size_t> GetStripIndices() const;

        bool IsDocked(std::size_t index) const noexcept;
        void SetDocked(std::size_t index, bool docked);

        std::optional<std::size_t> GetActiveIndex() const noexcept;
        void SetActiveIndex(std::size_t index);

        // Restores the invariant that the active index is unset or points at an
        // open, docked entry. Every mutator ends here, so no caller can forget it.
        void ReconcileActiveIndex();

        // Isolated entries that are still meant to be on screen, in entry order.
        std::vector<std::size_t> GetDetachedIndices() const;

        // True when at least one entry would occupy the row body this frame.
        bool HasVisibleDockedPanel() const noexcept;

    private:
        bool IsRenderable(std::size_t index) const noexcept;

        std::deque<EditorToolRowEntry> entries_;
        std::optional<std::size_t> active_;
    };
}

#endif // KPENGINE_EDITOR_TOOL_ROW_MODEL_H
