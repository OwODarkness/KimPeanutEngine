#ifndef KPENGINE_EDITOR_WINDOW_VISIBILITY_H
#define KPENGINE_EDITOR_WINDOW_VISIBILITY_H

namespace kpengine::editor
{
    // Shared open/closed state for one editor surface. A tab's close button, a
    // floating window's title-bar X, and a View-menu checkmark all read and write
    // this one value, so they cannot disagree.
    //
    // Deliberately ImGui-free and header-only: the tool-row model borrows these and
    // must stay testable without linking ImGui.
    class EditorWindowVisibility
    {
    public:
        explicit EditorWindowVisibility(bool open = false) noexcept : open_(open) {}

        bool IsOpen() const noexcept { return open_; }
        void SetOpen(bool open) noexcept { open_ = open; }
        void Toggle() noexcept { open_ = !open_; }

    private:
        bool open_ = false;
    };
}

#endif // KPENGINE_EDITOR_WINDOW_VISIBILITY_H
