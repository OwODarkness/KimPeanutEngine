#include "editor/ui/component/editor_layout_model.h"

#include <algorithm>

namespace kpengine::editor
{
    namespace
    {
        constexpr int kLeafBase = -1;  // child encoding: -(slot + 1)

        int Leaf(EditorLayoutSlot slot) noexcept
        {
            return -(static_cast<int>(slot) + 1);
        }

        // The status bar's height is content-derived (font height plus padding), not a
        // fraction, which is why the root split is a fixed-pixel split.
        constexpr float kStatusBarDefaultPixels = 43.0f;

        // The right column's three stacked panels are 35% / 35% / 30% of its height, so
        // the lower split's fraction is the debug viewer's share of the remaining 65%.
        constexpr float kCameraDefaultFraction = 0.35f;
        constexpr float kDebugDefaultFraction = 0.35f / 0.65f;

        // The tool row takes the bottom 30% of the left-of-right column, so it runs all
        // the way down to the status bar instead of stopping at a ratio that disagreed
        // with the bar's pixel height.
        constexpr float kToolRowTopFraction = 0.70f;

        // The left column is 22% of the full width, and the top area is 80% of it.
        constexpr float kLeftColumnDefaultFraction = 0.22f / 0.80f;
        constexpr float kOutlinerDefaultFraction = 0.28f / 0.70f;
    }

    float ClampFirstExtent(float desired, float extent, float min_first,
                           float min_second) noexcept
    {
        // `!(extent > 0)` rather than `extent <= 0` so a NaN extent takes this branch
        // instead of falling through to std::clamp with a NaN bound.
        if (!(extent > 0.0f))
        {
            return 0.0f;
        }

        const float lower = std::min(std::max(min_first, 0.0f), extent);
        const float upper = extent - std::min(std::max(min_second, 0.0f), extent);
        if (upper < lower)
        {
            const float first_min = std::max(min_first, 0.0f);
            const float second_min = std::max(min_second, 0.0f);
            const float total = first_min + second_min;
            return total > 0.0f ? extent * (first_min / total) : extent * 0.5f;
        }
        return std::clamp(desired, lower, upper);
    }

    EditorLayoutModel::EditorLayoutModel()
    {
        ResetToDefault();
    }

    void EditorLayoutModel::ResetToDefault()
    {
        for (EditorLayoutNode &node : nodes_)
        {
            node = EditorLayoutNode{};
        }
        node_count_ = 7;

        // 0: work area minus the status bar.
        nodes_[0].axis = EditorLayoutAxis::Vertical;
        nodes_[0].fixed_pixels = kStatusBarDefaultPixels;
        nodes_[0].splitter = EditorSplitterId::StatusBar;
        nodes_[0].first = 1;
        nodes_[0].second = Leaf(EditorLayoutSlot::ProfileBar);

        // 1: left-of-right column vs the right column.
        nodes_[1].axis = EditorLayoutAxis::Horizontal;
        nodes_[1].fraction = 0.80f;
        nodes_[1].splitter = EditorSplitterId::RightColumn;
        nodes_[1].min_first = 240.0f;
        nodes_[1].min_second = 160.0f;
        nodes_[1].first = 2;
        nodes_[1].second = 5;

        // 2: top area vs the tool row.
        nodes_[2].axis = EditorLayoutAxis::Vertical;
        nodes_[2].fraction = kToolRowTopFraction;
        nodes_[2].splitter = EditorSplitterId::ToolRow;
        nodes_[2].min_first = 160.0f;
        nodes_[2].min_second = 100.0f;
        nodes_[2].first = 3;
        nodes_[2].second = Leaf(EditorLayoutSlot::ToolRow);

        // 3: left column vs the viewport.
        nodes_[3].axis = EditorLayoutAxis::Horizontal;
        nodes_[3].fraction = kLeftColumnDefaultFraction;
        nodes_[3].splitter = EditorSplitterId::LeftColumn;
        nodes_[3].min_first = 140.0f;
        nodes_[3].min_second = 200.0f;
        nodes_[3].first = 4;
        nodes_[3].second = Leaf(EditorLayoutSlot::Viewport);

        // 4: world outliner vs actor inspector.
        nodes_[4].axis = EditorLayoutAxis::Vertical;
        nodes_[4].fraction = kOutlinerDefaultFraction;
        nodes_[4].splitter = EditorSplitterId::Outliner;
        nodes_[4].min_first = 80.0f;
        nodes_[4].min_second = 80.0f;
        nodes_[4].first = Leaf(EditorLayoutSlot::WorldOutliner);
        nodes_[4].second = Leaf(EditorLayoutSlot::ActorInspector);

        // 5: camera settings vs the lower right column.
        nodes_[5].axis = EditorLayoutAxis::Vertical;
        nodes_[5].fraction = kCameraDefaultFraction;
        nodes_[5].splitter = EditorSplitterId::Camera;
        nodes_[5].min_first = 80.0f;
        nodes_[5].min_second = 120.0f;
        nodes_[5].first = Leaf(EditorLayoutSlot::CameraSettings);
        nodes_[5].second = 6;

        // 6: debug viewer vs the GPU profiler.
        nodes_[6].axis = EditorLayoutAxis::Vertical;
        nodes_[6].fraction = kDebugDefaultFraction;
        nodes_[6].splitter = EditorSplitterId::Debug;
        nodes_[6].min_first = 80.0f;
        nodes_[6].min_second = 80.0f;
        nodes_[6].first = Leaf(EditorLayoutSlot::DebugViewer);
        nodes_[6].second = Leaf(EditorLayoutSlot::GpuProfiler);

        for (EditorRect &rect : rects_)
        {
            rect = EditorRect{};
        }
        for (EditorRect &rect : node_rects_)
        {
            rect = EditorRect{};
        }
        for (EditorRect &rect : seams_)
        {
            rect = EditorRect{};
        }

    }

    void EditorLayoutModel::ResolveNode(int node_index, const EditorRect &rect)
    {
        if (node_index < 0 || node_index >= node_count_)
        {
            return;
        }

        node_rects_[node_index] = rect;
        const EditorLayoutNode &node = nodes_[node_index];

        const bool horizontal = node.axis == EditorLayoutAxis::Horizontal;
        const float extent = horizontal ? rect.width : rect.height;

        float first_extent = 0.0f;
        float second_extent = 0.0f;
        if (node.fixed_pixels > 0.0f)
        {
            // A fixed second child can never exceed its parent, so a work area shorter
            // than the status bar yields an empty bar and an empty workspace rather than
            // a negative extent.
            second_extent = std::min(node.fixed_pixels, std::max(0.0f, extent));
            first_extent = std::max(0.0f, extent - second_extent);
        }
        else
        {
            // Clamped here as well as in the drag: resizing the OS window changes the
            // extent a stored fraction is measured against, so resolve-time is the only
            // place that can guarantee a child never falls below its minimum.
            first_extent = ClampFirstExtent(extent * node.fraction, extent, node.min_first,
                                            node.min_second);
            second_extent = std::max(0.0f, extent - first_extent);
        }

        EditorRect first_rect = rect;
        EditorRect second_rect = rect;
        if (horizontal)
        {
            first_rect.width = first_extent;
            second_rect.x = rect.x + first_extent;
            second_rect.width = second_extent;
        }
        else
        {
            first_rect.height = first_extent;
            second_rect.y = rect.y + first_extent;
            second_rect.height = second_extent;
        }

        // Record where the two children actually meet, for the handle to sit on. Taken
        // from the resolved rects rather than from the fraction, so a clamped split still
        // puts its handle on the visible edge.
        if (node.splitter != EditorSplitterId::None)
        {
            EditorRect seam{};
            if (horizontal)
            {
                seam = EditorRect{first_rect.x + first_rect.width, rect.y, 0.0f, rect.height};
            }
            else
            {
                seam = EditorRect{rect.x, first_rect.y + first_rect.height, rect.width, 0.0f};
            }
            seams_[static_cast<std::size_t>(node.splitter)] = seam;
        }

        const auto resolve_child = [this](int child, const EditorRect &child_rect)
        {
            if (child >= 0)
            {
                ResolveNode(child, child_rect);
            }
            else if (child <= kLeafBase)
            {
                const int slot = -child + kLeafBase;
                if (slot >= 0 && slot < static_cast<int>(kEditorLayoutSlotCount))
                {
                    rects_[slot] = child_rect;
                }
            }
        };
        resolve_child(node.first, first_rect);
        resolve_child(node.second, second_rect);
    }

    void EditorLayoutModel::Resolve(const EditorRect &work_area)
    {
        for (EditorRect &rect : rects_)
        {
            rect = EditorRect{};
        }
        for (EditorRect &rect : node_rects_)
        {
            rect = EditorRect{};
        }
        for (EditorRect &rect : seams_)
        {
            rect = EditorRect{};
        }

        // Sanitise first. A split only ever rewrites the extent along its own axis, so a
        // negative or NaN extent on the other axis would otherwise be inherited by every
        // descendant and surface as a negative-width rect rather than an empty one.
        EditorRect area = work_area;
        if (!(area.width > 0.0f))
        {
            area.width = 0.0f;
        }
        if (!(area.height > 0.0f))
        {
            area.height = 0.0f;
        }
        ResolveNode(0, area);
    }

    const EditorRect &EditorLayoutModel::RectOf(EditorLayoutSlot slot) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(slot);
        if (index >= kEditorLayoutSlotCount)
        {
            static const EditorRect empty{};
            return empty;
        }
        return rects_[index];
    }

    bool EditorLayoutModel::HasSlot(EditorLayoutSlot slot) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(slot);
        return index < kEditorLayoutSlotCount && !rects_[index].IsEmpty();
    }

    bool EditorLayoutModel::IsDock(EditorLayoutSlot slot) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(slot);
        return index < kEditorLayoutSlotCount && slot != EditorLayoutSlot::ProfileBar;
    }

    EditorLayoutSlot EditorLayoutModel::HitTestDock(float x, float y) const noexcept
    {
        // Half-open on the far edges, unlike EditorRect::Contains. Docks TILE the work
        // area, so an inclusive test would make every shared edge belong to two docks
        // and hand the answer to whichever happens to sit earlier in the enum. A
        // partition needs [min, max), which gives every point exactly one dock.
        //
        // The consequence is that a point on the outermost right or bottom edge of the
        // work area belongs to no dock and falls through to a float, which is the same
        // answer as dropping just outside the window.
        for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
        {
            const auto slot = static_cast<EditorLayoutSlot>(index);
            if (!IsDock(slot) || !HasSlot(slot))
            {
                continue;
            }
            const EditorRect &rect = rects_[index];
            if (x >= rect.x && x < rect.x + rect.width && y >= rect.y &&
                y < rect.y + rect.height)
            {
                return slot;
            }
        }
        return EditorLayoutSlot::Count;
    }

    int EditorLayoutModel::FindSplitterNode(EditorSplitterId id) const noexcept
    {
        if (id == EditorSplitterId::None)
        {
            return -1;
        }
        for (int index = 0; index < node_count_; ++index)
        {
            if (nodes_[index].splitter == id)
            {
                return index;
            }
        }
        return -1;
    }

    float EditorLayoutModel::SplitterFraction(EditorSplitterId id) const noexcept
    {
        const int index = FindSplitterNode(id);
        return index < 0 ? 0.0f : nodes_[index].fraction;
    }

    bool EditorLayoutModel::IsSplitterDraggable(EditorSplitterId id) const noexcept
    {
        const int index = FindSplitterNode(id);
        if (index < 0)
        {
            return false;
        }
        // A fixed-extent split has no fraction to drag: the status bar's height comes
        // from its content.
        return nodes_[index].fixed_pixels <= 0.0f;
    }

    EditorRect EditorLayoutModel::SplitterRect(EditorSplitterId id) const noexcept
    {
        const int index = FindSplitterNode(id);
        if (index < 0)
        {
            return EditorRect{};
        }
        return node_rects_[index];
    }

    EditorLayoutAxis EditorLayoutModel::SplitterAxis(EditorSplitterId id) const noexcept
    {
        const int index = FindSplitterNode(id);
        return index < 0 ? EditorLayoutAxis::Horizontal : nodes_[index].axis;
    }

    EditorRect EditorLayoutModel::SplitterHandleRect(EditorSplitterId id,
                                                     float thickness) const noexcept
    {
        const std::size_t slot = static_cast<std::size_t>(id);
        if (id == EditorSplitterId::None || slot >= kEditorSplitterCount ||
            thickness <= 0.0f)
        {
            return EditorRect{};
        }

        const EditorRect &seam = seams_[slot];
        const float half = thickness * 0.5f;
        if (SplitterAxis(id) == EditorLayoutAxis::Horizontal)
        {
            return EditorRect{seam.x - half, seam.y, thickness, seam.height};
        }
        return EditorRect{seam.x, seam.y - half, seam.width, thickness};
    }

    float EditorLayoutModel::ResolveSplitterDrag(EditorSplitterId id, float start_fraction,
                                                 float delta_px) const
    {
        const int index = FindSplitterNode(id);
        if (index < 0 || !IsSplitterDraggable(id))
        {
            return index < 0 ? 0.0f : nodes_[index].fraction;
        }

        const EditorLayoutNode &node = nodes_[index];
        const EditorRect &parent = node_rects_[index];
        const float extent =
            node.axis == EditorLayoutAxis::Horizontal ? parent.width : parent.height;
        if (!(extent > 0.0f))
        {
            return node.fraction;
        }

        // The same clamp the resolver uses, expressed in pixels, so a drag and a resolve
        // can never disagree about how small a child may become.
        const float first_extent = ClampFirstExtent((start_fraction + delta_px / extent) * extent,
                                                    extent, node.min_first, node.min_second);
        return first_extent / extent;
    }

    void EditorLayoutModel::ApplySplitterDrag(EditorSplitterId id, float start_fraction,
                                              float delta_px)
    {
        const int index = FindSplitterNode(id);
        if (index < 0 || !IsSplitterDraggable(id))
        {
            return;
        }
        const float resolved = ResolveSplitterDrag(id, start_fraction, delta_px);
        nodes_[index].fraction = resolved;
    }

    void EditorLayoutModel::SetFixedExtentPixels(EditorSplitterId id, float pixels)
    {
        const int index = FindSplitterNode(id);
        if (index < 0)
        {
            return;
        }
        nodes_[index].fixed_pixels = std::max(0.0f, pixels);
    }

    void EditorLayoutModel::SetSplitterFraction(EditorSplitterId id, float fraction)
    {
        const int index = FindSplitterNode(id);
        if (index < 0 || !IsSplitterDraggable(id))
        {
            return;
        }
        nodes_[index].fraction = std::clamp(fraction, 0.0f, 1.0f);
    }

    const char *EditorLayoutModel::SplitterKey(EditorSplitterId id) noexcept
    {
        switch (id)
        {
        case EditorSplitterId::RightColumn:
            return "right_column";
        // StatusBar deliberately has no key: a measurement must not be persisted.
        case EditorSplitterId::ToolRow:
            return "tool_row";
        case EditorSplitterId::LeftColumn:
            return "left_column";
        case EditorSplitterId::Outliner:
            return "outliner";
        case EditorSplitterId::Camera:
            return "camera";
        case EditorSplitterId::Debug:
            return "debug";
        case EditorSplitterId::None:
        case EditorSplitterId::Count:
            break;
        }
        return "";
    }

    EditorSplitterId EditorLayoutModel::SplitterFromKey(std::string_view key) noexcept
    {
        for (std::size_t index = 0; index < kEditorSplitterCount; ++index)
        {
            const auto id = static_cast<EditorSplitterId>(index);
            const char *const candidate = SplitterKey(id);
            if (candidate != nullptr && candidate[0] != '\0' && key == candidate)
            {
                return id;
            }
        }
        return EditorSplitterId::None;
    }

    const char *EditorLayoutModel::RegionKey(EditorLayoutSlot slot) noexcept
    {
        switch (slot)
        {
        case EditorLayoutSlot::WorldOutliner:
            return "world_outliner";
        case EditorLayoutSlot::ActorInspector:
            return "actor_inspector";
        case EditorLayoutSlot::Viewport:
            return "viewport";
        case EditorLayoutSlot::ToolRow:
            return "tool_row";
        case EditorLayoutSlot::CameraSettings:
            return "camera_settings";
        case EditorLayoutSlot::DebugViewer:
            return "debug_viewer";
        case EditorLayoutSlot::GpuProfiler:
            return "gpu_profiler";
        case EditorLayoutSlot::ProfileBar:
            return "profile_bar";
        case EditorLayoutSlot::Count:
            break;
        }
        return "";
    }

    EditorLayoutSlot EditorLayoutModel::RegionFromKey(std::string_view key) noexcept
    {
        for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
        {
            const auto slot = static_cast<EditorLayoutSlot>(index);
            const char *const candidate = RegionKey(slot);
            if (candidate != nullptr && candidate[0] != '\0' && key == candidate)
            {
                return slot;
            }
        }
        return EditorLayoutSlot::Count;
    }
}
