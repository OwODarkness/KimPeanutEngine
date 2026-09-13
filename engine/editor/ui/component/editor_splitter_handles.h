#ifndef KPENGINE_EDITOR_SPLITTER_HANDLES_H
#define KPENGINE_EDITOR_SPLITTER_HANDLES_H

#include "editor/ui/component/editor_layout_model.h"

namespace kpengine::editor
{
    // Draws and drags the seams between layout regions.
    //
    // The handles are NOT windows. They are strips of the foreground draw list plus a raw
    // mouse test, evaluated after every panel has rendered. A host window would have been
    // the obvious alternative and would have brought two bugs with it: ImGui routes the
    // mouse wheel to the hovered window, so a full-area overlay would stop every panel
    // scrolling; and clicking a panel's title calls BringToFrontOnFocus, so a panel would
    // be raised permanently above the handles.
    //
    // All the arithmetic lives in EditorLayoutModel and is unit-tested. What is here is
    // only hit-testing, the cursor, and the drag accumulator.
    class EditorSplitterHandles final
    {
    public:
        // Called once per frame, after the workspace panels have rendered.
        void Render(EditorLayoutModel &model);

        // True while a seam is being dragged, so the caller can persist on release
        // instead of writing the layout file every frame.
        bool IsDragging() const noexcept { return dragging_; }

        // True on the single frame a drag ends, which is when the layout is worth saving.
        bool ConsumeDragJustEnded() noexcept;

    private:
        bool dragging_ = false;
        bool drag_just_ended_ = false;
        std::size_t active_split_ = 0;
        float drag_start_fraction_ = 0.0f;
        float drag_start_mouse_ = 0.0f;
    };
}

#endif // KPENGINE_EDITOR_SPLITTER_HANDLES_H
