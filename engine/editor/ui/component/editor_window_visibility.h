#ifndef KPENGINE_EDITOR_WINDOW_VISIBILITY_H
#define KPENGINE_EDITOR_WINDOW_VISIBILITY_H

#include "editor/ui/component/editor_panel_content.h"

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
        bool IsCloseDeferred() const noexcept { return close_deferred_; }
        void SetOpen(bool open) noexcept
        {
            open_ = open;
            close_deferred_ = false;
        }
        void Toggle() noexcept { SetOpen(!open_); }

        void RequestClose(EditorPanelCloseDecision decision) noexcept
        {
            switch (decision)
            {
            case EditorPanelCloseDecision::Accept:
                SetOpen(false);
                break;
            case EditorPanelCloseDecision::Defer:
                close_deferred_ = true;
                break;
            case EditorPanelCloseDecision::Cancel:
                close_deferred_ = false;
                break;
            case EditorPanelCloseDecision::NoDecision:
                break;
            }
        }
        void ResolveDeferredClose(bool accept) noexcept
        {
            if (close_deferred_)
            {
                close_deferred_ = false;
                if (accept)
                {
                    SetOpen(false);
                }
            }
        }

    private:
        bool open_ = false;
        bool close_deferred_ = false;
    };
}

#endif // KPENGINE_EDITOR_WINDOW_VISIBILITY_H
