#ifndef KPENGINE_EDITOR_LAYOUT_MODEL_H
#define KPENGINE_EDITOR_LAYOUT_MODEL_H

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "editor/ui/component/editor_layout_rect.h"

// Must stay ImGui-free: the whole point of ED2 is that the layout arithmetic —
// tiling, reflow, and splitter clamping — is unit-testable without a frame.
// Including editor_window_component.h here would pull in imgui.h and break that.

namespace kpengine::editor
{
    // One leaf per panel. A leaf is what a component binds to.
    enum class EditorLayoutSlot : std::uint8_t
    {
        WorldOutliner,
        ActorInspector,
        Viewport,
        ToolRow,
        CameraSettings,
        DebugViewer,
        GpuProfiler,
        ProfileBar,
        Count,
    };

    inline constexpr std::size_t kEditorLayoutSlotCount =
        static_cast<std::size_t>(EditorLayoutSlot::Count);

    enum class EditorLayoutAxis : std::uint8_t
    {
        Horizontal,  // splits along x: first child left, second right
        Vertical,    // splits along y: first child top, second bottom
    };

    // One id per split. None is not a split; StatusBar exists so the editor can push the
    // bar's measured height each frame, but it is fixed-extent hence never draggable and
    // never persisted.
    enum class EditorSplitterId : std::uint8_t
    {
        None,
        StatusBar,
        RightColumn,
        ToolRow,
        LeftColumn,
        Outliner,
        Camera,
        Debug,
        Count,
    };

    inline constexpr std::size_t kEditorSplitterCount =
        static_cast<std::size_t>(EditorSplitterId::Count);

    // Clamps the FIRST child's extent so both children keep their minima and the two
    // always tile the parent exactly. Both the resolver and the splitter drag go through
    // here, so a stored fraction can never describe a child smaller than its minimum —
    // which is reachable without a drag, because resizing the OS window changes the
    // extent a fraction is measured against.
    //
    // When the parent is genuinely too small for both minima they cannot both hold;
    // rather than starve one child the deficit is shared in proportion to the minima,
    // which degenerates to an even split when they are equal.
    float ClampFirstExtent(float desired, float extent, float min_first,
                           float min_second) noexcept;

    // A node in the declarative layout table. `fraction` is relative to the PARENT
    // extent along `axis`, which is what makes reflow automatic: a child's rect derives
    // from its parent's, so resizing the work area or moving any splitter updates
    // everything below it with no extra bookkeeping.
    struct EditorLayoutNode
    {
        EditorLayoutAxis axis = EditorLayoutAxis::Horizontal;
        float fraction = 0.5f;
        // When > 0 the SECOND child gets this many pixels instead of a fraction. Used by
        // the status bar, whose height is content-derived rather than proportional.
        float fixed_pixels = 0.0f;
        EditorSplitterId splitter = EditorSplitterId::None;
        // Splitter clamp in pixels. A split can never be dragged past these.
        float min_first = 0.0f;
        float min_second = 0.0f;
        int first = -1;   // child node index, or -(slot + 1) for a leaf
        int second = -1;
    };

    class EditorLayoutModel final
    {
    public:
        EditorLayoutModel();

        // Restores the default tree and clears every resolved rect.
        void ResetToDefault();

        // Resolves every slot against a work area. Safe for a degenerate area: every
        // slot resolves empty rather than producing negative extents.
        void Resolve(const EditorRect &work_area);

        const EditorRect &RectOf(EditorLayoutSlot slot) const noexcept;
        bool HasSlot(EditorLayoutSlot slot) const noexcept;

        float SplitterFraction(EditorSplitterId id) const noexcept;
        bool IsSplitterDraggable(EditorSplitterId id) const noexcept;
        // Resolves the rect of the split's PARENT, which is what its handle spans.
        EditorRect SplitterRect(EditorSplitterId id) const noexcept;
        EditorLayoutAxis SplitterAxis(EditorSplitterId id) const noexcept;

        // The strip a handle occupies: a zero-extent line on the edge the two children
        // share, widened to `thickness` across the split axis. Derived from the RESOLVED
        // children rather than from the fraction, so a clamped split puts its handle
        // where the edge actually is.
        EditorRect SplitterHandleRect(EditorSplitterId id, float thickness) const noexcept;

        // Pure clamp arithmetic: the fraction that dragging `delta_px` from
        // `start_fraction` produces. Takes the drag's START fraction rather than adding to
        // the current one, so a drag driven from the accumulated mouse delta each frame
        // cannot drift. Returns the current fraction when the split is not draggable,
        // unknown, or the parent extent is degenerate.
        float ResolveSplitterDrag(EditorSplitterId id, float start_fraction, float delta_px) const;

        // Applies ResolveSplitterDrag and re-resolves.
        void ApplySplitterDrag(EditorSplitterId id, float start_fraction, float delta_px);

        void SetSplitterFraction(EditorSplitterId id, float fraction);

        // Sets a fixed-extent split's pixel amount. The editor pushes the status bar's
        // measured height here every frame, because that height comes from the font and
        // theme metrics rather than from a ratio.
        void SetFixedExtentPixels(EditorSplitterId id, float pixels);

        // Stable string keys, so a persisted layout survives node reordering.
        static const char *SplitterKey(EditorSplitterId id) noexcept;
        static EditorSplitterId SplitterFromKey(std::string_view key) noexcept;

    private:
        void ResolveNode(int node_index, const EditorRect &rect);
        int FindSplitterNode(EditorSplitterId id) const noexcept;

        EditorLayoutNode nodes_[16]{};
        int node_count_ = 0;
        EditorRect rects_[kEditorLayoutSlotCount]{};
        EditorRect node_rects_[16]{};
        // Zero-extent seam lines, one per split id, filled during Resolve.
        EditorRect seams_[kEditorSplitterCount]{};
    };
}

#endif // KPENGINE_EDITOR_LAYOUT_MODEL_H
