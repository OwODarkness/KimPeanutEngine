#include "editor/ui/component/editor_tool_row_model.h"

#include <algorithm>

namespace kpengine::editor
{
    EditorToolDropTarget ResolveToolRowDrop(const EditorRect &row, float x, float y) noexcept
    {
        // A row that has not been laid out yet has no area to drop into. Answering
        // None keeps a collapsed row from swallowing the drop as "dock".
        if (row.width <= 0.0f || row.height <= 0.0f)
        {
            return EditorToolDropTarget::None;
        }

        const bool inside = x >= row.x && x <= row.x + row.width &&
                            y >= row.y && y <= row.y + row.height;
        return inside ? EditorToolDropTarget::TabStrip : EditorToolDropTarget::Float;
    }

    bool EditorToolRowModel::IsRenderable(std::size_t index) const noexcept
    {
        if (index >= entries_.size())
        {
            return false;
        }
        const EditorToolRowEntry &entry = entries_[index];
        return entry.visibility.IsOpen() && entry.docked;
    }

    std::size_t EditorToolRowModel::AddEntry(std::string id, std::string title, bool open,
                                             bool docked)
    {
        if (const std::optional<std::size_t> existing = IndexOf(id))
        {
            return *existing;
        }

        EditorToolRowEntry entry;
        entry.id = std::move(id);
        entry.title = std::move(title);
        entry.visibility.SetOpen(open);
        entry.docked = docked;
        entries_.push_back(std::move(entry));

        ReconcileActiveIndex();
        return entries_.size() - 1;
    }

    void EditorToolRowModel::Clear() noexcept
    {
        entries_.clear();
        active_.reset();
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
        ReconcileActiveIndex();
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

    bool EditorToolRowModel::ShowInRowById(std::string_view id)
    {
        const std::optional<std::size_t> index = IndexOf(id);
        if (!index.has_value())
        {
            return false;
        }
        // Open first, then dock: SetDocked only promotes an ALREADY-OPEN entry to active,
        // so docking first would leave a closed entry docked but not shown.
        SetOpen(*index, true);
        SetDocked(*index, true);
        return true;
    }

    std::vector<std::size_t> EditorToolRowModel::GetStripIndices() const
    {
        std::vector<std::size_t> strip;
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const EditorToolRowEntry &entry = entries_[index];
            if (entry.visibility.IsOpen() && entry.docked)
            {
                strip.push_back(index);
            }
        }
        return strip;
    }

    bool EditorToolRowModel::IsDocked(std::size_t index) const noexcept
    {
        const EditorToolRowEntry *const entry = GetEntry(index);
        return entry != nullptr && entry->docked;
    }

    void EditorToolRowModel::SetDocked(std::size_t index, bool docked)
    {
        EditorToolRowEntry *const entry = GetEntryMutable(index);
        if (entry == nullptr)
        {
            return;
        }
        entry->docked = docked;

        // Re-docking an open panel brings it forward. A context-menu "Dock" that
        // left the panel hidden behind another tab would read as broken.
        if (docked && entry->visibility.IsOpen())
        {
            active_ = index;
        }
        ReconcileActiveIndex();
    }

    std::optional<std::size_t> EditorToolRowModel::GetActiveIndex() const noexcept
    {
        return active_;
    }

    void EditorToolRowModel::SetActiveIndex(std::size_t index)
    {
        // Only a renderable entry can be active, which keeps the invariant from
        // being reachable by a stale index from the render loop.
        if (!IsRenderable(index))
        {
            return;
        }
        active_ = index;
    }

    void EditorToolRowModel::ReconcileActiveIndex()
    {
        if (active_.has_value() && IsRenderable(*active_))
        {
            return;
        }

        const std::size_t count = entries_.size();
        if (count == 0)
        {
            active_.reset();
            return;
        }

        // Search forward from the last position, then backward, so closing the
        // active tab selects its right-hand neighbour and closing the last one
        // falls back to its left-hand neighbour. Deterministic, with no memory of
        // what used to be active.
        const std::size_t start = active_.value_or(0);
        for (std::size_t index = start; index < count; ++index)
        {
            if (IsRenderable(index))
            {
                active_ = index;
                return;
            }
        }
        for (std::size_t index = start; index-- > 0;)
        {
            if (IsRenderable(index))
            {
                active_ = index;
                return;
            }
        }
        active_.reset();
    }

    std::vector<std::size_t> EditorToolRowModel::GetDetachedIndices() const
    {
        std::vector<std::size_t> detached;
        for (std::size_t index = 0; index < entries_.size(); ++index)
        {
            const EditorToolRowEntry &entry = entries_[index];
            if (entry.visibility.IsOpen() && !entry.docked)
            {
                detached.push_back(index);
            }
        }
        return detached;
    }

    bool EditorToolRowModel::HasVisibleDockedPanel() const noexcept
    {
        return std::any_of(entries_.begin(), entries_.end(),
                           [](const EditorToolRowEntry &entry)
                           { return entry.visibility.IsOpen() && entry.docked; });
    }
}
