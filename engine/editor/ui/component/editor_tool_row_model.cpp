#include "editor/ui/component/editor_tool_row_model.h"

#include <algorithm>

namespace kpengine::editor
{
    EditorPlacementTarget ResolvePlacementDrop(const EditorLayoutModel &layout, float x,
                                               float y) noexcept
    {
        EditorPlacementTarget target;

        // A dock under the cursor is the whole answer. Whether it is empty does not matter
        // any more: a dock that already holds panels takes this one as another tab, which
        // is what removed the "a drop never displaces a panel" rule ED3 was built around.
        const EditorLayoutSlot dock = layout.HitTestDock(x, y);
        if (dock != EditorLayoutSlot::Count)
        {
            target.kind = EditorPlacementTargetKind::Dock;
            target.dock = dock;
            return target;
        }

        // Nowhere resolved at all: the layout has not run this frame, so there are no
        // rectangles on screen to aim at and a float ghost would be a promise the release
        // could not keep. The bottom row is the canary — it is resolved whenever the
        // layout runs.
        if (!layout.HasSlot(EditorLayoutSlot::ToolRow))
        {
            return target;
        }

        // Outside every dock: the panel stands alone, which is the behaviour ED1 shipped
        // for dragging a tab out of the row.
        target.kind = EditorPlacementTargetKind::Float;
        return target;
    }

    namespace
    {
        bool IsMemberOfDock(const EditorToolRowModel &model, std::size_t index,
                            EditorLayoutSlot dock)
        {
            const EditorToolRowEntry *const entry = model.GetEntry(index);
            return entry != nullptr && entry->dock == dock && entry->visibility.IsOpen();
        }
    }

    std::size_t EditorToolRowModel::AddEntry(std::string id, std::string title, bool open,
                                             EditorLayoutSlot dock)
    {
        if (const std::optional<std::size_t> existing = IndexOf(id))
        {
            return *existing;
        }

        EditorToolRowEntry entry;
        entry.id = std::move(id);
        entry.title = std::move(title);
        entry.visibility.SetOpen(open);
        // A panel cannot start in somewhere that is not a dock. Falling back rather than
        // refusing keeps registration total: a bad dock is a wiring mistake, and losing the
        // panel entirely would be a worse way to report it.
        entry.dock = EditorLayoutModel::IsDock(dock) ? std::optional<EditorLayoutSlot>{dock}
                                                     : std::optional<EditorLayoutSlot>{
                                                           EditorLayoutSlot::ToolRow};
        entries_.push_back(std::move(entry));

        ReconcileActive();
        return entries_.size() - 1;
    }

    void EditorToolRowModel::Clear() noexcept
    {
        entries_.clear();
        dock_locked_.fill(false);
        for (std::optional<std::size_t> &active : active_)
        {
            active.reset();
        }
        focus_request_.reset();
    }

    std::size_t EditorToolRowModel::GetEntryCount() const noexcept
    {
        return entries_.size();
    }

    const EditorToolRowEntry *EditorToolRowModel::GetEntry(std::size_t index) const noexcept
    {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

    EditorToolRowEntry *EditorToolRowModel::GetEntryMutable(std::size_t index) noexcept
    {
        return index < entries_.size() ? &entries_[index] : nullptr;
    }

    const EditorToolRowEntry *EditorToolRowModel::FindEntry(std::string_view id) const noexcept
    {
        for (const EditorToolRowEntry &entry : entries_)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    std::optional<std::size_t> EditorToolRowModel::IndexOf(std::string_view id) const noexcept
    {
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            if (entries_[index].id == id)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    bool EditorToolRowModel::IsOpen(std::size_t index) const noexcept
    {
        const EditorToolRowEntry *const entry = GetEntry(index);
        return entry != nullptr && entry->visibility.IsOpen();
    }

    void EditorToolRowModel::SetOpen(std::size_t index, bool open)
    {
        EditorToolRowEntry *const entry = GetEntryMutable(index);
        if (entry == nullptr)
        {
            return;
        }
        entry->visibility.SetOpen(open);
        if (!open && focus_request_.has_value() && *focus_request_ == index)
        {
            focus_request_.reset();
        }
        ReconcileActive();
    }

    void EditorToolRowModel::ToggleOpen(std::size_t index)
    {
        const EditorToolRowEntry *const entry = GetEntry(index);
        if (entry == nullptr)
        {
            return;
        }
        SetOpen(index, !entry->visibility.IsOpen());
    }

    bool EditorToolRowModel::IsOpenById(std::string_view id) const noexcept
    {
        const EditorToolRowEntry *const entry = FindEntry(id);
        return entry != nullptr && entry->visibility.IsOpen();
    }

    void EditorToolRowModel::ToggleOpenById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (index.has_value())
        {
            ToggleOpen(*index);
        }
    }

    void EditorToolRowModel::SetOpenById(std::string_view id, bool open)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (index.has_value())
        {
            SetOpen(*index, open);
        }
    }

    bool EditorToolRowModel::ShowById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (!index.has_value())
        {
            return false;
        }

        EditorToolRowEntry *const entry = GetEntryMutable(*index);
        if (entry == nullptr)
        {
            return false;
        }
        entry->visibility.SetOpen(true);
        if (entry->dock.has_value())
        {
            active_[static_cast<std::size_t>(*entry->dock)] = *index;
        }
        else
        {
            focus_request_ = *index;
        }
        ReconcileActive();
        return true;
    }

    bool EditorToolRowModel::FocusById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (!index.has_value() || !IsOpen(*index))
        {
            return false;
        }

        EditorToolRowEntry *const entry = GetEntryMutable(*index);
        if (entry == nullptr)
        {
            return false;
        }
        if (entry->dock.has_value())
        {
            active_[static_cast<std::size_t>(*entry->dock)] = *index;
        }
        else
        {
            focus_request_ = *index;
        }
        return true;
    }

    bool EditorToolRowModel::ConsumeFocusRequest(std::size_t index) noexcept
    {
        if (!focus_request_.has_value() || *focus_request_ != index)
        {
            return false;
        }
        focus_request_.reset();
        return true;
    }

    bool EditorToolRowModel::ShowInRowById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (!index.has_value())
        {
            return false;
        }
        // Unchecked, deliberately: the lock gates DRAGGING, and this is not a drag. A
        // locked panel must still be recoverable, or the lock would be a way to lose a
        // panel — the same terminal-state trap ED1's window close and ED2's region panels
        // each fell into. Carry the effective lock onto the destination container so an
        // explicit recovery action does not silently make a locked panel draggable.
        const bool was_locked = IsLocked(*index);
        const bool moved = MoveToDockUnchecked(*index, EditorLayoutSlot::ToolRow);
        if (moved && was_locked)
        {
            SetDockLocked(EditorLayoutSlot::ToolRow, true);
        }
        return moved;
    }

    bool EditorToolRowModel::IsLocked(std::size_t index) const noexcept
    {
        const EditorToolRowEntry *const entry = GetEntry(index);
        if (entry == nullptr)
        {
            return false;
        }
        if (entry->dock.has_value())
        {
            return IsDockLocked(*entry->dock);
        }
        return entry->locked;
    }

    bool EditorToolRowModel::IsLockedById(std::string_view id) const noexcept
    {
        const std::optional<std::size_t> index = IndexOf(id);
        return index.has_value() && IsLocked(*index);
    }

    bool EditorToolRowModel::IsDockLocked(EditorLayoutSlot dock) const noexcept
    {
        if (!EditorLayoutModel::IsDock(dock))
        {
            return false;
        }
        return dock_locked_[static_cast<std::size_t>(dock)];
    }

    void EditorToolRowModel::SetDockLocked(EditorLayoutSlot dock, bool locked)
    {
        if (!EditorLayoutModel::IsDock(dock))
        {
            return;
        }
        bool &state = dock_locked_[static_cast<std::size_t>(dock)];
        if (state == locked)
        {
            return;
        }
        state = locked;
        BumpPlacementRevision();
    }

    void EditorToolRowModel::ToggleDockLocked(EditorLayoutSlot dock)
    {
        SetDockLocked(dock, !IsDockLocked(dock));
    }

    void EditorToolRowModel::SetLocked(std::size_t index, bool locked)
    {
        EditorToolRowEntry *const entry = GetEntryMutable(index);
        if (entry == nullptr)
        {
            return;
        }
        if (entry->dock.has_value())
        {
            SetDockLocked(*entry->dock, locked);
            return;
        }
        if (entry->locked == locked)
        {
            return;
        }
        entry->locked = locked;
        // Persisted, so a lock change is a change worth writing.
        BumpPlacementRevision();
    }

    void EditorToolRowModel::SetLockedById(std::string_view id, bool locked)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (index.has_value())
        {
            SetLocked(*index, locked);
        }
    }

    void EditorToolRowModel::ToggleLockedById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (index.has_value())
        {
            SetLocked(*index, !IsLocked(*index));
        }
    }

    std::optional<EditorLayoutSlot> EditorToolRowModel::GetDock(std::size_t index) const noexcept
    {
        const EditorToolRowEntry *const entry = GetEntry(index);
        if (entry == nullptr)
        {
            return std::nullopt;
        }
        return entry->dock;
    }

    std::optional<EditorLayoutSlot> EditorToolRowModel::GetDockById(
        std::string_view id) const noexcept
    {
        const EditorToolRowEntry *const entry = FindEntry(id);
        if (entry == nullptr)
        {
            return std::nullopt;
        }
        return entry->dock;
    }

    std::vector<std::size_t> EditorToolRowModel::GetDockMembers(EditorLayoutSlot dock) const
    {
        std::vector<std::size_t> members;
        if (!EditorLayoutModel::IsDock(dock))
        {
            return members;
        }
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            if (IsMemberOfDock(*this, index, dock))
            {
                members.push_back(index);
            }
        }
        return members;
    }

    bool EditorToolRowModel::IsDockOccupied(EditorLayoutSlot dock) const noexcept
    {
        if (!EditorLayoutModel::IsDock(dock))
        {
            return false;
        }
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            if (IsMemberOfDock(*this, index, dock))
            {
                return true;
            }
        }
        return false;
    }

    bool EditorToolRowModel::HasVisibleDockedPanel() const noexcept
    {
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const EditorToolRowEntry &entry = entries_[index];
            // Floating panels are not "docked", so a workspace where everything floats has
            // no occupied dock — and no dock windows to draw.
            if (entry.dock.has_value() && entry.visibility.IsOpen())
            {
                return true;
            }
        }
        return false;
    }

    std::vector<std::size_t> EditorToolRowModel::GetFloatingIndices() const
    {
        std::vector<std::size_t> floating;
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const EditorToolRowEntry &entry = entries_[index];
            if (entry.visibility.IsOpen() && !entry.dock.has_value())
            {
                floating.push_back(index);
            }
        }
        return floating;
    }

    bool EditorToolRowModel::MoveToDockUnchecked(std::size_t index, EditorLayoutSlot dock)
    {
        if (!EditorLayoutModel::IsDock(dock))
        {
            return false;
        }
        EditorToolRowEntry *const entry = GetEntryMutable(index);
        if (entry == nullptr)
        {
            return false;
        }

        const bool changed =
            entry->dock != std::optional<EditorLayoutSlot>{dock} || !entry->visibility.IsOpen();
        entry->visibility.SetOpen(true);
        entry->dock = dock;
        focus_request_.reset();
        if (changed)
        {
            BumpPlacementRevision();
        }

        // The panel that just moved comes forward in its new dock. A drop that left the
        // panel hidden behind another tab would read as broken.
        if (const std::size_t slot = static_cast<std::size_t>(dock);
            slot < kEditorLayoutSlotCount)
        {
            active_[slot] = index;
        }
        ReconcileActive();
        return true;
    }

    bool EditorToolRowModel::MoveToDock(std::size_t index, EditorLayoutSlot dock)
    {
        // The lock gate. Refusing rather than half-applying means a locked panel cannot be
        // moved by any route the gesture can reach.
        if (IsLocked(index))
        {
            return false;
        }
        return MoveToDockUnchecked(index, dock);
    }

    bool EditorToolRowModel::MoveToDockById(std::string_view id, EditorLayoutSlot dock)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        return index.has_value() && MoveToDock(*index, dock);
    }

    bool EditorToolRowModel::FloatPanel(std::size_t index)
    {
        EditorToolRowEntry *const entry = GetEntryMutable(index);
        if (entry == nullptr || IsLocked(index))
        {
            return false;
        }
        if (!entry->dock.has_value() && entry->visibility.IsOpen())
        {
            return false;  // already standing alone
        }

        entry->dock.reset();
        // Floating a closed panel would move a window nobody can see, so floating opens it.
        entry->visibility.SetOpen(true);
        BumpPlacementRevision();
        ReconcileActive();
        return true;
    }

    std::optional<std::size_t> EditorToolRowModel::GetActiveInDock(
        EditorLayoutSlot dock) const noexcept
    {
        if (!EditorLayoutModel::IsDock(dock))
        {
            return std::nullopt;
        }
        return active_[static_cast<std::size_t>(dock)];
    }

    void EditorToolRowModel::SetActiveInDock(EditorLayoutSlot dock, std::size_t index)
    {
        if (!EditorLayoutModel::IsDock(dock) || !IsMemberOfDock(*this, index, dock))
        {
            return;
        }
        active_[static_cast<std::size_t>(dock)] = index;
    }

    void EditorToolRowModel::ReconcileActive()
    {
        for (std::size_t slot = 0; slot < kEditorLayoutSlotCount; ++slot)
        {
            const auto dock = static_cast<EditorLayoutSlot>(slot);
            if (!EditorLayoutModel::IsDock(dock))
            {
                active_[slot].reset();
                continue;
            }
            if (active_[slot].has_value() && IsMemberOfDock(*this, *active_[slot], dock))
            {
                continue;
            }

            // Forward from the last position, then backward, so closing the active tab
            // selects its right-hand neighbour and closing the last one falls back to its
            // left-hand neighbour. Deterministic, and with no memory of what used to be
            // active. Scoped to this dock: another dock's panels are not neighbours here.
            const std::size_t start = active_[slot].value_or(0);
            std::optional<std::size_t> found;
            for (std::size_t index = start; index < entries_.size() && !found.has_value(); ++index)
            {
                if (IsMemberOfDock(*this, index, dock))
                {
                    found = index;
                }
            }
            for (std::size_t index = start; index-- > 0 && !found.has_value();)
            {
                if (IsMemberOfDock(*this, index, dock))
                {
                    found = index;
                }
            }
            active_[slot] = found;
        }
    }
}
