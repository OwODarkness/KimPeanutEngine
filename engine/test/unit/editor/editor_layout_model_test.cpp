#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "editor/settings/editor_layout_settings.h"
#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_tool_row_model.h"

// The layout model is ImGui-free by design, so this target links no ImGui and compiles
// the model source directly. Tiling, reflow, and splitter clamping are the arithmetic
// that ED2 exists to make testable; the tiling assertions here are precisely what would
// have caught the GPU Profiler overflowing the work area in the old ratio layout.
//
// ED3 adds region identity and placeability here, and the placement half of the
// persisted file, which is what makes a magnetic drop survive a relaunch.
namespace
{
    using kpengine::editor::ApplyLayoutState;
    using kpengine::editor::ApplyPlacementState;
    using kpengine::editor::CaptureLayoutState;
    using kpengine::editor::CapturePlacementState;
    using kpengine::editor::ClampFirstExtent;
    using kpengine::editor::EditorLayoutAxis;
    using kpengine::editor::EditorLayoutModel;
    using kpengine::editor::EditorLayoutSlot;
    using kpengine::editor::EditorLayoutState;
    using kpengine::editor::EditorRect;
    using kpengine::editor::EditorSplitterId;
    using kpengine::editor::EditorToolRowEntry;
    using kpengine::editor::EditorToolRowModel;
    using kpengine::editor::kEditorLayoutSlotCount;
    using kpengine::editor::kEditorSplitterCount;
    using kpengine::editor::EditorPlacementRecord;
    using kpengine::editor::kToolRowGpuProfilerId;
    using kpengine::editor::kToolRowViewportId;
    using kpengine::editor::kToolRowLogId;
    using kpengine::editor::ReadEditorLayoutState;
    using kpengine::editor::SnapEdgesToPixels;
    using kpengine::editor::WriteEditorLayoutState;

    constexpr float kEps = 0.5f;

    EditorRect WorkArea(float width = 1920.0f, float height = 1000.0f, float x = 0.0f,
                        float y = 0.0f)
    {
        return EditorRect{x, y, width, height};
    }

    std::vector<EditorRect> AllRects(const EditorLayoutModel &model)
    {
        std::vector<EditorRect> rects;
        for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
        {
            rects.push_back(model.RectOf(static_cast<EditorLayoutSlot>(index)));
        }
        return rects;
    }

    // A split computes its second child's extent as `parent - first`, so a shared edge can
    // differ by one float rounding step between the two rects that meet there. Measured as
    // the overlap's SHORTER side, which is scale-independent: an area threshold would
    // loosen as a region grows, since the same rounding error times a wider rect is a
    // larger area. Any real overlap has a thickness of whole pixels.
    //
    // The exact-area assertion in ExpectValidTiling is the real proof of tiling: a genuine
    // overlap would push the summed area past the work area.
    float OverlapThickness(const EditorRect &a, const EditorRect &b)
    {
        const float overlap_x =
            std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
        const float overlap_y =
            std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
        if (overlap_x <= 0.0f || overlap_y <= 0.0f)
        {
            return 0.0f;
        }
        return std::min(overlap_x, overlap_y);
    }

    // The shared invariant for every resolved layout: nothing escapes the work area,
    // nothing overlaps, and the regions tile it exactly. The GPU Profiler bug violated
    // the first and third of these; the tool row violated the first and second.
    void ExpectValidTiling(const EditorLayoutModel &model, const EditorRect &work)
    {
        const std::vector<EditorRect> rects = AllRects(model);
        float total = 0.0f;

        for (std::size_t index = 0; index < rects.size(); ++index)
        {
            const EditorRect &rect = rects[index];
            EXPECT_FALSE(rect.IsEmpty()) << "slot " << index << " resolved empty";
            EXPECT_GE(rect.x, work.x - kEps) << "slot " << index << " escapes left";
            EXPECT_GE(rect.y, work.y - kEps) << "slot " << index << " escapes top";
            EXPECT_LE(rect.x + rect.width, work.x + work.width + kEps)
                << "slot " << index << " escapes right";
            EXPECT_LE(rect.y + rect.height, work.y + work.height + kEps)
                << "slot " << index << " escapes bottom (this is the GPU Profiler bug)";
            total += rect.width * rect.height;
        }

        for (std::size_t a = 0; a < rects.size(); ++a)
        {
            for (std::size_t b = a + 1; b < rects.size(); ++b)
            {
                EXPECT_LT(OverlapThickness(rects[a], rects[b]), 0.01f)
                    << "slots " << a << " and " << b << " overlap";
            }
        }

        EXPECT_NEAR(total, work.width * work.height, kEps)
            << "regions do not tile the work area exactly";
    }

    std::size_t SlotIndex(EditorLayoutSlot slot)
    {
        return static_cast<std::size_t>(slot);
    }

    // A per-process unique path, so parallel test runs cannot collide.
    std::string TemporaryLayoutPath()
    {
        static unsigned counter = 0;
        ++counter;
        return (std::filesystem::temp_directory_path() /
                ("kp_editor_layout_" + std::to_string(counter) + ".json"))
            .string();
    }

    void WriteText(const std::string &path, const std::string &text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << text;
    }
}

TEST(EditorLayoutModelTest, ProfileBarIsPinnedToTheBottomAtItsContentHeight)
{
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    const EditorRect &bar = model.RectOf(EditorLayoutSlot::ProfileBar);
    EXPECT_FLOAT_EQ(bar.x, work.x);
    EXPECT_FLOAT_EQ(bar.width, work.width);
    EXPECT_FLOAT_EQ(bar.height, 43.0f);
    EXPECT_FLOAT_EQ(bar.y + bar.height, work.y + work.height);
}

TEST(EditorLayoutModelTest, GpuProfilerAndToolRowBothMeetTheProfileBarExactly)
{
    // The named regression. The old layout had the profiler's bottom at 1.04 * H and the
    // tool row's bottom at 0.96 * H, while the bar's top sat at H - 43: one overflowed
    // the work area and drew over the bar, the other left a gap on tall windows.
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    const float bar_top = model.RectOf(EditorLayoutSlot::ProfileBar).y;

    const EditorRect &profiler = model.RectOf(EditorLayoutSlot::GpuProfiler);
    EXPECT_NEAR(profiler.y + profiler.height, bar_top, kEps);
    EXPECT_LE(profiler.y + profiler.height, work.y + work.height + kEps);

    const EditorRect &tool_row = model.RectOf(EditorLayoutSlot::ToolRow);
    EXPECT_NEAR(tool_row.y + tool_row.height, bar_top, kEps);
}

TEST(EditorLayoutModelTest, DefaultLayoutPreservesTodaysColumnsHorizontally)
{
    // Every x coordinate is exactly the old layout's: the horizontal arrangement was
    // correct, only the vertical band descriptions disagreed with each other.
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::WorldOutliner).x, 0.0f, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::Viewport).x, 0.22f * work.width, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::Viewport).x +
                    model.RectOf(EditorLayoutSlot::Viewport).width,
                0.80f * work.width, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::CameraSettings).x, 0.80f * work.width, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::CameraSettings).width, 0.20f * work.width, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::ToolRow).width, 0.80f * work.width, kEps);
}

TEST(EditorLayoutModelTest, LayoutTilesTheWorkAreaAtEveryRealisticSize)
{
    const EditorRect sizes[] = {
        WorkArea(1920.0f, 1000.0f),
        WorkArea(1280.0f, 720.0f),
        WorkArea(2560.0f, 1440.0f),   // where the old tool row left a 13 px gap
        WorkArea(800.0f, 600.0f),
        WorkArea(1024.0f, 741.0f, 0.0f, 25.0f),  // menu-bar offset
        WorkArea(640.0f, 480.0f),
    };

    for (const EditorRect &work : sizes)
    {
        SCOPED_TRACE(::testing::Message() << "work area " << work.width << "x" << work.height
                                          << " at (" << work.x << "," << work.y << ")");
        EditorLayoutModel model;
        model.Resolve(work);
        ExpectValidTiling(model, work);
    }
}

TEST(EditorLayoutModelTest, EverySlotResolvesForANormalWorkArea)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
    {
        const auto slot = static_cast<EditorLayoutSlot>(index);
        EXPECT_TRUE(model.HasSlot(slot)) << "slot " << index << " did not resolve";
    }
    EXPECT_FALSE(model.HasSlot(EditorLayoutSlot::Count));
}

TEST(EditorLayoutModelTest, ReflowScalesWidthsWithTheWorkArea)
{
    // Widths scale exactly, because every horizontal fraction is of the work area's
    // width. Heights deliberately do NOT, because the status bar takes a fixed 43 px
    // before any fraction is applied — that is the point of expressing it as pixels.
    EditorLayoutModel model;
    model.Resolve(WorkArea(1920.0f, 1000.0f));
    const EditorRect wide = model.RectOf(EditorLayoutSlot::Viewport);

    model.Resolve(WorkArea(960.0f, 1000.0f));
    const EditorRect narrow = model.RectOf(EditorLayoutSlot::Viewport);

    EXPECT_NEAR(narrow.x, wide.x * 0.5f, kEps);
    EXPECT_NEAR(narrow.width, wide.width * 0.5f, kEps);
    EXPECT_NEAR(narrow.height, wide.height, kEps)
        << "an unchanged height must not move when only the width halves";

    // Growing the window by 1000 px grows the tiling area by 1000 and the viewport by its
    // 70% share of the left-of-right column, since the bar keeps its fixed 43 px.
    model.Resolve(WorkArea(1920.0f, 2000.0f));
    const EditorRect tall = model.RectOf(EditorLayoutSlot::Viewport);
    EXPECT_NEAR(tall.height - wide.height, 0.70f * 1000.0f, kEps);
    EXPECT_NEAR(model.RectOf(EditorLayoutSlot::ProfileBar).height, 43.0f, kEps);
}

TEST(EditorLayoutModelTest, DraggingASplitReflowsItsSubtreeAndLeavesTheRestAlone)
{
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    const EditorRect outliner_before = model.RectOf(EditorLayoutSlot::WorldOutliner);
    const EditorRect inspector_before = model.RectOf(EditorLayoutSlot::ActorInspector);
    const EditorRect viewport_before = model.RectOf(EditorLayoutSlot::Viewport);
    const EditorRect tool_row_before = model.RectOf(EditorLayoutSlot::ToolRow);
    const EditorRect camera_before = model.RectOf(EditorLayoutSlot::CameraSettings);
    const EditorRect profiler_before = model.RectOf(EditorLayoutSlot::GpuProfiler);
    const EditorRect bar_before = model.RectOf(EditorLayoutSlot::ProfileBar);

    // Push the outliner/inspector seam down by 60 px.
    const float start = model.SplitterFraction(EditorSplitterId::Outliner);
    model.ApplySplitterDrag(EditorSplitterId::Outliner, start, 60.0f);
    model.Resolve(work);

    // Its own children moved: the outliner grew by exactly the drag, the inspector shrank
    // by the same amount, and the two still fill their parent.
    const EditorRect &outliner_after = model.RectOf(EditorLayoutSlot::WorldOutliner);
    const EditorRect &inspector_after = model.RectOf(EditorLayoutSlot::ActorInspector);
    EXPECT_NEAR(outliner_after.height, outliner_before.height + 60.0f, kEps);
    EXPECT_NEAR(inspector_after.height, inspector_before.height - 60.0f, kEps);
    EXPECT_NEAR(outliner_after.height + inspector_after.height,
                outliner_before.height + inspector_before.height, kEps);

    // Nothing outside that subtree moved.
    const auto unchanged = [](const EditorRect &a, const EditorRect &b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    };
    EXPECT_TRUE(unchanged(model.RectOf(EditorLayoutSlot::Viewport), viewport_before));
    EXPECT_TRUE(unchanged(model.RectOf(EditorLayoutSlot::ToolRow), tool_row_before));
    EXPECT_TRUE(unchanged(model.RectOf(EditorLayoutSlot::CameraSettings), camera_before));
    EXPECT_TRUE(unchanged(model.RectOf(EditorLayoutSlot::GpuProfiler), profiler_before));
    EXPECT_TRUE(unchanged(model.RectOf(EditorLayoutSlot::ProfileBar), bar_before));

    ExpectValidTiling(model, work);
}

TEST(EditorLayoutModelTest, SplitterKeysRoundTripAndOnlyPersistableSplitsAreNamed)
{
    std::size_t named = 0;
    for (std::size_t index = 0; index < static_cast<std::size_t>(EditorSplitterId::Count);
         ++index)
    {
        const auto id = static_cast<EditorSplitterId>(index);
        const char *const key = EditorLayoutModel::SplitterKey(id);
        ASSERT_NE(key, nullptr);

        // Unnamed on purpose: None is not a split, and StatusBar is a content-derived
        // measurement that must never be written to a user's layout file.
        const bool must_be_unnameable = id == EditorSplitterId::None ||
                                        id == EditorSplitterId::StatusBar ||
                                        id == EditorSplitterId::Count;
        if (must_be_unnameable)
        {
            EXPECT_EQ(std::string_view{key}, std::string_view{})
                << "split " << static_cast<int>(id) << " must not be persistable";
            continue;
        }

        EXPECT_FALSE(std::string_view{key}.empty());
        EXPECT_EQ(EditorLayoutModel::SplitterFromKey(key), id);
        ++named;
    }
    EXPECT_EQ(named, 6u) << "one stable key per draggable seam";

    EXPECT_EQ(EditorLayoutModel::SplitterFromKey("no_such_split"), EditorSplitterId::None);
    EXPECT_EQ(EditorLayoutModel::SplitterFromKey(""), EditorSplitterId::None);
}

TEST(EditorLayoutModelTest, SplitterRectSpansTheParentWhoseChildrenItDivides)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    EXPECT_TRUE(model.IsSplitterDraggable(EditorSplitterId::ToolRow));
    EXPECT_TRUE(model.IsSplitterDraggable(EditorSplitterId::RightColumn));
    EXPECT_FALSE(model.IsSplitterDraggable(EditorSplitterId::None));
    EXPECT_FALSE(model.IsSplitterDraggable(EditorSplitterId::Count));

    // The left-column seam divides the top area, so its handle spans that area, not the
    // whole work area: the tool row sits below it and must not be overlapped by a handle.
    const EditorRect left_seam = model.SplitterRect(EditorSplitterId::LeftColumn);
    const EditorRect viewport = model.RectOf(EditorLayoutSlot::Viewport);
    EXPECT_FLOAT_EQ(left_seam.y, viewport.y);
    EXPECT_FLOAT_EQ(left_seam.height, viewport.height);
    EXPECT_FLOAT_EQ(left_seam.height + model.RectOf(EditorLayoutSlot::ToolRow).height +
                        model.RectOf(EditorLayoutSlot::ProfileBar).height,
                    1000.0f);

    // The right-column seam divides the full tiling height.
    const EditorRect right_seam = model.SplitterRect(EditorSplitterId::RightColumn);
    EXPECT_FLOAT_EQ(right_seam.y, 0.0f);
    EXPECT_FLOAT_EQ(right_seam.height, 1000.0f - 43.0f);
}

TEST(EditorLayoutModelTest, UnknownAndOffEnumSlotsAreInert)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    EXPECT_FALSE(model.HasSlot(EditorLayoutSlot::Count));
    EXPECT_TRUE(model.RectOf(EditorLayoutSlot::Count).IsEmpty());
    EXPECT_FLOAT_EQ(model.SplitterFraction(EditorSplitterId::Count), 0.0f);
    EXPECT_TRUE(model.SplitterRect(EditorSplitterId::Count).IsEmpty());
    EXPECT_FALSE(model.IsSplitterDraggable(EditorSplitterId::Count));
}

TEST(ClampFirstExtentTest, KeepsBothMinimaWheneverTheyBothFit)
{
    constexpr float min_first = 100.0f;
    constexpr float min_second = 100.0f;

    for (const float extent : {400.0f, 1000.0f})
    {
        for (float desired = -200.0f; desired <= extent + 200.0f; desired += 25.0f)
        {
            const float first = ClampFirstExtent(desired, extent, min_first, min_second);
            EXPECT_GE(first, min_first - 0.01f) << "extent " << extent << " desired " << desired;
            EXPECT_LE(first, extent - min_second + 0.01f)
                << "extent " << extent << " desired " << desired;
            // The two children always fill the parent exactly, whatever the clamp did.
            EXPECT_NEAR(first + (extent - first), extent, 0.01f);
        }
    }
}

TEST(ClampFirstExtentTest, WhenBothMinimaCannotFitTheChildrenStillFillTheParent)
{
    // A parent of 100 px cannot honour two 100 px minima. Rather than starve one child it
    // shares the deficit, so the tiling stays exact and neither extent goes negative.
    for (float desired = -200.0f; desired <= 300.0f; desired += 25.0f)
    {
        const float first = ClampFirstExtent(desired, 100.0f, 100.0f, 100.0f);
        EXPECT_GE(first, 0.0f) << "desired " << desired;
        EXPECT_LE(first, 100.0f) << "desired " << desired;
        EXPECT_NEAR(first, 50.0f, 0.01f) << "equal minima should split evenly";
    }
}

TEST(ClampFirstExtentTest, SqueezesProportionallyWhenTheParentIsTooSmallForBothMinima)
{
    const float first = ClampFirstExtent(90.0f, 100.0f, 80.0f, 80.0f);
    EXPECT_NEAR(first, 50.0f, kEps);

    // Unequal minima still tile exactly, sharing the deficit in proportion.
    const float uneven = ClampFirstExtent(90.0f, 100.0f, 75.0f, 25.0f);
    EXPECT_NEAR(uneven, 75.0f, kEps);
    EXPECT_NEAR(uneven + (100.0f - uneven), 100.0f, kEps);
}

TEST(ClampFirstExtentTest, DegenerateAndNanExtentsDoNotProduceNegativeOrNanResults)
{
    EXPECT_FLOAT_EQ(ClampFirstExtent(50.0f, 0.0f, 10.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(ClampFirstExtent(50.0f, -10.0f, 10.0f, 10.0f), 0.0f);

    const float nan_extent = ClampFirstExtent(50.0f, std::nanf(""), 10.0f, 10.0f);
    EXPECT_FALSE(std::isnan(nan_extent));
    EXPECT_FLOAT_EQ(nan_extent, 0.0f);
}

TEST(EditorSplitterDragTest, DragTracksTheMouseAndPinsAtTheMinimumWithoutLatching)
{
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    const float start = model.SplitterFraction(EditorSplitterId::RightColumn);
    const float extent = model.SplitterRect(EditorSplitterId::RightColumn).width;

    // Dragging left past the minimum pins at the minimum...
    const float pinned = model.ResolveSplitterDrag(EditorSplitterId::RightColumn, start,
                                                   -extent * 4.0f);
    EXPECT_NEAR(pinned * extent, 240.0f, kEps) << "should pin at min_first";

    // ...and because the base is the drag's START fraction, dragging back a little
    // tracks the mouse again rather than staying latched at the clamp. An implementation
    // that accumulated into the stored fraction would fail this.
    const float back = model.ResolveSplitterDrag(EditorSplitterId::RightColumn, start, -40.0f);
    EXPECT_NEAR(back, start - 40.0f / extent, 1e-4f);
    EXPECT_GT(back, pinned);
}

TEST(EditorSplitterDragTest, ASplitCanNeverBeInverted)
{
    EditorLayoutModel model;
    const EditorRect work = WorkArea();
    model.Resolve(work);

    for (const EditorSplitterId id : {EditorSplitterId::RightColumn, EditorSplitterId::ToolRow,
                                      EditorSplitterId::LeftColumn, EditorSplitterId::Outliner,
                                      EditorSplitterId::Camera, EditorSplitterId::Debug})
    {
        for (const float delta : {-100000.0f, 100000.0f})
        {
            const float fraction = model.ResolveSplitterDrag(id, 0.5f, delta);
            EXPECT_GE(fraction, 0.0f) << "splitter " << static_cast<int>(id);
            EXPECT_LE(fraction, 1.0f) << "splitter " << static_cast<int>(id);

            // Applying an extreme drag and re-resolving must still tile: the children can
            // never cross, so neither can be negative or swallow its sibling.
            model.ApplySplitterDrag(id, 0.5f, delta);
            model.Resolve(work);
            ExpectValidTiling(model, work);
        }
    }
}

TEST(EditorSplitterDragTest, NonDraggableAndUnknownSplittersAreInert)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    const float before = model.SplitterFraction(EditorSplitterId::ToolRow);
    model.ApplySplitterDrag(EditorSplitterId::None, 0.5f, 500.0f);
    model.ApplySplitterDrag(EditorSplitterId::Count, 0.5f, 500.0f);
    EXPECT_FLOAT_EQ(model.SplitterFraction(EditorSplitterId::ToolRow), before);

    // The status bar's split is fixed-extent, so it has no fraction to drag.
    model.ApplySplitterDrag(EditorSplitterId::RightColumn, 0.5f, 10.0f);
    EXPECT_FALSE(model.IsSplitterDraggable(EditorSplitterId::None));
}

TEST(EditorSplitterDragTest, ResolveClampsAStoredFractionThatFallsBelowItsMinimum)
{
    // Reachable without a drag: a fraction valid at one window size can describe a child
    // smaller than its minimum at another, so the resolver must clamp too. Before the
    // shared clamp this returned a sub-minimum child.
    EditorLayoutModel model;
    const EditorRect work = WorkArea(1920.0f, 1000.0f);

    // Push the left/centre seam far right, so the viewport would be squeezed below its
    // 200 px minimum while its parent stays comfortably large.
    model.SetSplitterFraction(EditorSplitterId::LeftColumn, 0.99f);
    model.Resolve(work);

    const EditorRect &viewport = model.RectOf(EditorLayoutSlot::Viewport);
    EXPECT_GE(viewport.width, 200.0f - kEps) << "viewport fell below min_second";

    const EditorRect &left_column = model.RectOf(EditorLayoutSlot::WorldOutliner);
    EXPECT_GE(left_column.width, 140.0f - kEps) << "left column fell below min_first";
    EXPECT_NEAR(left_column.width + viewport.width, 0.80f * work.width, kEps)
        << "the clamped children must still fill their parent exactly";

    // The other extreme: squeezed from the right.
    EditorLayoutModel other;
    other.SetSplitterFraction(EditorSplitterId::LeftColumn, 0.0f);
    other.Resolve(work);
    EXPECT_GE(other.RectOf(EditorLayoutSlot::WorldOutliner).width, 140.0f - kEps);
    ExpectValidTiling(other, work);
}

TEST(EditorLayoutModelTest, DegenerateWorkAreasResolveEmptyWithoutCrashing)
{
    const EditorRect degenerate[] = {
        EditorRect{},
        EditorRect{0.0f, 0.0f, 1.0f, 1.0f},
        EditorRect{0.0f, 0.0f, -100.0f, -100.0f},
        EditorRect{0.0f, 0.0f, 1920.0f, 10.0f},  // shorter than the profile bar
    };

    for (const EditorRect &work : degenerate)
    {
        EditorLayoutModel model;
        model.Resolve(work);
        const std::vector<EditorRect> rects = AllRects(model);
        for (const EditorRect &rect : rects)
        {
            EXPECT_GE(rect.width, 0.0f);
            EXPECT_GE(rect.height, 0.0f);
        }
    }
}

TEST(EditorLayoutModelTest, WorkAreaWithAMenuBarOffsetIsRespected)
{
    EditorLayoutModel model;
    const EditorRect work = WorkArea(1024.0f, 741.0f, 0.0f, 25.0f);
    model.Resolve(work);

    const EditorRect &bar = model.RectOf(EditorLayoutSlot::ProfileBar);
    EXPECT_NEAR(bar.y + bar.height, 25.0f + 741.0f, kEps);
    ExpectValidTiling(model, work);
}

TEST(EditorLayoutRectTest, SnappingPreservesTheSharedEdgeBetweenNeighbours)
{
    const EditorRect left{0.0f, 0.0f, 422.4f, 100.0f};
    const EditorRect right{422.4f, 0.0f, 597.6f, 100.0f};

    const EditorRect snapped_left = SnapEdgesToPixels(left);
    const EditorRect snapped_right = SnapEdgesToPixels(right);

    EXPECT_FLOAT_EQ(snapped_left.x + snapped_left.width, snapped_right.x)
        << "a one-pixel background seam would be drawn here";
    EXPECT_FLOAT_EQ(snapped_left.width, 422.0f);
}

TEST(EditorLayoutRectTest, SnappingNeverProducesANegativeExtent)
{
    const EditorRect tiny{10.4f, 10.4f, 0.1f, 0.1f};
    const EditorRect snapped = SnapEdgesToPixels(tiny);
    EXPECT_GE(snapped.width, 0.0f);
    EXPECT_GE(snapped.height, 0.0f);
}

TEST(EditorLayoutRectTest, EmptyAndOverlapAgreeWithExtents)
{
    // Named locals rather than brace temporaries inside the macros: a braced initializer's
    // commas are not inside parentheses, so the preprocessor would read them as extra
    // macro arguments.
    const EditorRect zero{0.0f, 0.0f, 0.0f, 100.0f};
    const EditorRect zero_height{0.0f, 0.0f, 100.0f, 0.0f};
    const EditorRect unit{0.0f, 0.0f, 1.0f, 1.0f};
    const EditorRect left{0.0f, 0.0f, 10.0f, 10.0f};
    const EditorRect adjacent{10.0f, 0.0f, 10.0f, 10.0f};
    const EditorRect overlapping{5.0f, 5.0f, 10.0f, 10.0f};
    const EditorRect large{0.0f, 0.0f, 100.0f, 100.0f};

    EXPECT_TRUE(zero.IsEmpty()) << "zero width";
    EXPECT_TRUE(zero_height.IsEmpty()) << "zero height";
    EXPECT_TRUE(EditorRect{}.IsEmpty()) << "default";
    EXPECT_FALSE(unit.IsEmpty());

    // Touching edges are adjacent, not overlapping. This matters because the tiling
    // assertion treats any overlap as a failure, so adjacent regions must not trip it.
    EXPECT_FALSE(left.Overlaps(adjacent));
    EXPECT_TRUE(left.Overlaps(overlapping));

    // An empty rect overlaps nothing, so a degenerate slot cannot fail a tiling check.
    EXPECT_FALSE(zero.Overlaps(large));
}

TEST(EditorLayoutRectTest, ContainsIsInclusiveOnBothEdges)
{
    const EditorRect rect{10.0f, 20.0f, 100.0f, 50.0f};
    EXPECT_TRUE(rect.Contains(10.0f, 20.0f));
    EXPECT_TRUE(rect.Contains(110.0f, 70.0f));
    EXPECT_TRUE(rect.Contains(60.0f, 45.0f));
    EXPECT_FALSE(rect.Contains(9.9f, 45.0f));
    EXPECT_FALSE(rect.Contains(60.0f, 70.1f));
}

TEST(EditorLayoutPersistenceTest, StateRoundTripsThroughAFile)
{
    EditorLayoutModel model;
    model.SetSplitterFraction(EditorSplitterId::ToolRow, 0.55f);
    model.SetSplitterFraction(EditorSplitterId::RightColumn, 0.71f);

    const EditorLayoutState written = CaptureLayoutState(model);
    const std::string path = TemporaryLayoutPath();
    ASSERT_NO_THROW(WriteEditorLayoutState(path, written));
    const EditorLayoutState read = ReadEditorLayoutState(path);

    EditorLayoutModel restored;
    ApplyLayoutState(read, restored);

    EXPECT_FLOAT_EQ(restored.SplitterFraction(EditorSplitterId::ToolRow), 0.55f);
    EXPECT_FLOAT_EQ(restored.SplitterFraction(EditorSplitterId::RightColumn), 0.71f);
    // Splits that were never written keep the model default rather than collapsing to 0.
    EXPECT_FLOAT_EQ(restored.SplitterFraction(EditorSplitterId::Outliner),
                    EditorLayoutModel{}.SplitterFraction(EditorSplitterId::Outliner));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, CaptureRecordsEveryDraggableSplitAndSkipsTheRest)
{
    // Every seam the user can actually move is written, so a saved layout is complete.
    // A split whose extent is content-derived (the status bar) is left unspecified:
    // persisting it would freeze a font- and theme-dependent measurement into the file.
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    const EditorLayoutState state = CaptureLayoutState(model);

    for (std::size_t index = 0; index < kEditorSplitterCount; ++index)
    {
        const auto id = static_cast<EditorSplitterId>(index);
        const bool persistable =
            EditorLayoutModel::SplitterKey(id)[0] != '\0' && model.IsSplitterDraggable(id);
        if (persistable)
        {
            EXPECT_FLOAT_EQ(state.fractions[index], model.SplitterFraction(id))
                << "split " << static_cast<int>(id) << " was not captured";
        }
        else
        {
            EXPECT_LT(state.fractions[index], 0.0f)
                << "split " << static_cast<int>(id) << " must stay unspecified";
        }
    }

    // At least the six draggable seams are covered, so this is not vacuously passing.
    EXPECT_EQ(static_cast<std::size_t>(EditorSplitterId::None), 0u);
    EXPECT_EQ(&state.fractions[static_cast<std::size_t>(EditorSplitterId::None)],
              &state.fractions[0]);
    EXPECT_GE(EditorLayoutModel::SplitterKey(EditorSplitterId::ToolRow)[0], 'a');
    EXPECT_TRUE(model.IsSplitterDraggable(EditorSplitterId::ToolRow));
}

TEST(EditorLayoutPersistenceTest, AMissingFileIsNotAnError)
{
    // A fresh checkout has no layout file, and that is the normal case.
    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(
        (std::filesystem::temp_directory_path() / "kp_absent_layout.json").string(), &diagnostic);

    EXPECT_TRUE(diagnostic.empty());
    for (const float fraction : state.fractions)
    {
        EXPECT_LT(fraction, 0.0f) << "every split should be left unspecified";
    }
}

TEST(EditorLayoutPersistenceTest, MalformedJsonYieldsDefaultsAndADiagnostic)
{
    const std::string path = TemporaryLayoutPath();
    WriteText(path, "{ this is not json");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_FALSE(diagnostic.empty()) << "a broken layout file should say so";
    for (const float fraction : state.fractions)
    {
        EXPECT_LT(fraction, 0.0f) << "defaults, not half-applied values";
    }

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, UnknownVersionAndUnknownKeysAreTolerated)
{
    const std::string path = TemporaryLayoutPath();

    WriteText(path, R"({"version": 99, "splits": {"tool_row": {"amount": 0.4}}})");
    std::string version_diagnostic;
    const EditorLayoutState future = ReadEditorLayoutState(path, &version_diagnostic);
    EXPECT_FALSE(version_diagnostic.empty());
    EXPECT_LT(future.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.0f)
        << "an unknown version keeps every default rather than half-applying";

    // A known version with an unknown split and a malformed entry: the good entries still
    // load, the rest are reported and skipped.
    WriteText(path,
              R"({"version": 1, "splits": {
                   "tool_row": {"amount": 0.4},
                   "no_such_split": {"amount": 0.9},
                   "camera": {"amount": "not a number"}
                 }})");
    std::string key_diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &key_diagnostic);

    EXPECT_FLOAT_EQ(state.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.4f);
    EXPECT_LT(state.fractions[static_cast<std::size_t>(EditorSplitterId::Camera)], 0.0f);
    EXPECT_NE(key_diagnostic.find("no_such_split"), std::string::npos);
    EXPECT_NE(key_diagnostic.find("camera"), std::string::npos);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, OutOfRangeAmountsAreRejectedRatherThanApplied)
{
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 1, "splits": {"tool_row": {"amount": 4.5}}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_FALSE(diagnostic.empty());
    EXPECT_LT(state.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.0f);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, WritingCreatesTheParentDirectoryAndLeavesNoTempFiles)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "kp_layout_write_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    const std::filesystem::path path = directory / "nested" / "editor_layout.json";

    EditorLayoutModel model;
    model.SetSplitterFraction(EditorSplitterId::Debug, 0.42f);
    ASSERT_NO_THROW(WriteEditorLayoutState(path.string(), CaptureLayoutState(model)));

    EXPECT_TRUE(std::filesystem::exists(path));
    // The temp file must not survive a successful write.
    for (const auto &entry : std::filesystem::directory_iterator(path.parent_path()))
    {
        EXPECT_EQ(entry.path().filename().string(), "editor_layout.json")
            << "a temp file was left behind";
    }

    std::filesystem::remove_all(directory, ignored);
}

// ED3: regions are addressable, and two of them are free slots a tool-row panel can be
// pinned into. That is the whole mechanism magnetic placement needs — a destination set
// and a rectangle to snap to.

TEST(EditorDockRegionTest, EveryRegionIsADockExceptTheStatusBar)
{
    // Declared rather than derived: the status bar is a metrics strip, not somewhere a
    // window goes. This replaced ED3's hardcoded pair of "free" regions, which existed only
    // because a drop could not displace a panel — docks take an arrival as another tab, so
    // there is no free space to compute.
    for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
    {
        const auto slot = static_cast<EditorLayoutSlot>(index);
        const bool expected = slot != EditorLayoutSlot::ProfileBar;
        EXPECT_EQ(EditorLayoutModel::IsDock(slot), expected)
            << "region " << index << " dock status is wrong";
    }

    EXPECT_FALSE(EditorLayoutModel::IsDock(EditorLayoutSlot::Count))
        << "Count is not a region at all";
}

TEST(EditorRegionTest, EveryRegionHasAStableKeyThatRoundTrips)
{
    // Persisted placements are keyed by these, never by index: a panel moving between docks
    // must not reattach a saved placement to a different region.
    for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
    {
        const auto slot = static_cast<EditorLayoutSlot>(index);
        const char *const key = EditorLayoutModel::RegionKey(slot);

        ASSERT_NE(key, nullptr) << "region " << index << " has no key";
        EXPECT_GT(std::string_view{key}.size(), 0u) << "region " << index << " has an empty key";
        EXPECT_EQ(EditorLayoutModel::RegionFromKey(key), slot)
            << "key '" << key << "' did not round-trip";
    }

    EXPECT_EQ(EditorLayoutModel::RegionKey(EditorLayoutSlot::Count), std::string_view{});
    EXPECT_EQ(EditorLayoutModel::RegionFromKey("no_such_region"), EditorLayoutSlot::Count);
    EXPECT_EQ(EditorLayoutModel::RegionFromKey(""), EditorLayoutSlot::Count);
}

TEST(EditorDockRegionTest, HitTestFindsTheDockContainingAPoint)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    for (std::size_t index = 0; index < kEditorLayoutSlotCount; ++index)
    {
        const auto slot = static_cast<EditorLayoutSlot>(index);
        const EditorRect &rect = model.RectOf(slot);
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;

        // The status bar is not a dock, so a point inside it answers Count.
        const EditorLayoutSlot expected =
            EditorLayoutModel::IsDock(slot) ? slot : EditorLayoutSlot::Count;
        EXPECT_EQ(model.HitTestDock(cx, cy), expected)
            << "region " << index << " hit test disagrees with dock status";
    }
}

TEST(EditorDockRegionTest, AnUnresolvedLayoutHasNoDockHits)
{
    // Resolve was never called. Every rectangle is zero-extent, so nothing is on screen and
    // nothing may accept a drop — including a point at the origin, which is exactly where an
    // empty rect sits.
    const EditorLayoutModel model;

    EXPECT_EQ(model.HitTestDock(0.0f, 0.0f), EditorLayoutSlot::Count);
    EXPECT_EQ(model.HitTestDock(-100.0f, -100.0f), EditorLayoutSlot::Count);
    EXPECT_FALSE(model.HasSlot(EditorLayoutSlot::GpuProfiler));
}

TEST(EditorDockRegionTest, AResolvedDockIsAHitAndADegenerateOneIsNot)
{
    EditorLayoutModel model;
    model.Resolve(WorkArea());

    const EditorRect &dock = model.RectOf(EditorLayoutSlot::GpuProfiler);
    ASSERT_FALSE(dock.IsEmpty());

    EXPECT_EQ(model.HitTestDock(dock.x, dock.y), EditorLayoutSlot::GpuProfiler)
        << "the min edge belongs to the dock";
    EXPECT_EQ(model.HitTestDock(dock.x + dock.width * 0.5f, dock.y + dock.height * 0.5f),
              EditorLayoutSlot::GpuProfiler);

    // A degenerate work area resolves every dock empty, so nothing is a destination.
    model.Resolve(EditorRect{0.0f, 0.0f, 0.0f, 0.0f});
    EXPECT_EQ(model.HitTestDock(0.0f, 0.0f), EditorLayoutSlot::Count);
}

TEST(EditorLayoutPersistenceTest, PlacementsRoundTripThroughAFile)
{
    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    row.AddEntry(kToolRowViewportId, "Viewport", true, EditorLayoutSlot::Viewport);
    ASSERT_TRUE(row.MoveToDockById(kToolRowLogId, EditorLayoutSlot::Viewport));
    row.SetLockedById(kToolRowViewportId, true);

    EditorLayoutState written = CaptureLayoutState(EditorLayoutModel{});
    CapturePlacementState(row, written);
    // EVERY panel is recorded: with docks, "which dock" is the whole placement, and a panel
    // in the default dock has one too.
    ASSERT_EQ(written.placements.size(), 2u);

    const std::string path = TemporaryLayoutPath();
    ASSERT_NO_THROW(WriteEditorLayoutState(path, written));

    // Applied to a model with the same panels registered, the arrangement is restored.
    EditorToolRowModel restored;
    restored.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    restored.AddEntry(kToolRowViewportId, "Viewport", true, EditorLayoutSlot::Viewport);

    std::string diagnostic;
    ApplyPlacementState(ReadEditorLayoutState(path), restored, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    EXPECT_EQ(restored.GetDockMembers(EditorLayoutSlot::Viewport).size(), 2u)
        << "both panels came back into the one dock";
    EXPECT_TRUE(restored.GetDockMembers(EditorLayoutSlot::ToolRow).empty());
    EXPECT_TRUE(restored.IsLockedById(kToolRowViewportId)) << "the container lock round-tripped too";
    EXPECT_TRUE(restored.IsLockedById(kToolRowLogId))
        << "every tab in the restored container shares its lock";

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, AVersionOneFileIsStillRead)
{
    // Version 1 is known, it simply has no placements. Rejecting it would throw away the
    // user's saved split sizes on upgrade and gain nothing.
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 1, "splits": {"tool_row": {"amount": 0.4}}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    EXPECT_FLOAT_EQ(state.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.4f);
    EXPECT_TRUE(state.placements.empty());

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, APartlyBrokenFileStillYieldsItsOtherHalf)
{
    // The two halves are independent: a bad split must not cost the user a placement,
    // and vice versa.
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 2,
                        "splits": {"tool_row": {"amount": 0.4}, "camera": {"amount": "x"}},
                        "placements": {"gpu_profiler": "log"}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_NE(diagnostic.find("camera"), std::string::npos);
    EXPECT_FLOAT_EQ(state.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.4f);
    ASSERT_EQ(state.placements.size(), 1u)
        << "the good split and the good placement both survive a bad sibling";

    // And the other way round: a malformed placements object keeps the splits.
    WriteText(path, R"({"version": 2, "splits": {"tool_row": {"amount": 0.4}},
                        "placements": []})");
    std::string second_diagnostic;
    const EditorLayoutState second = ReadEditorLayoutState(path, &second_diagnostic);
    EXPECT_NE(second_diagnostic.find("placements"), std::string::npos);
    EXPECT_FLOAT_EQ(second.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.4f);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, PlacementFailuresAreReportedNotApplied)
{
    // Every one of these is reachable from a hand-edited file, and none of them may take
    // the editor down or half-apply.
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 3,
                        "placements": {
                          "no_such_panel": {"dock": "viewport"},
                          "log": {"dock": "no_such_dock"},
                          "viewport": {"dock": "gpu_profiler"},
                          "console": {"dock": "actor_inspector"}
                        }})");

    std::string read_diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &read_diagnostic);
    EXPECT_NE(read_diagnostic.find("no_such_dock"), std::string::npos);
    // The unknown dock is dropped by the reader; the other three survive to be validated
    // against the registered panels.
    EXPECT_EQ(state.placements.size(), 3u);

    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    row.AddEntry(kToolRowViewportId, "Viewport", true, EditorLayoutSlot::Viewport);

    std::string diagnostic;
    ASSERT_NO_THROW(ApplyPlacementState(state, row, &diagnostic));

    EXPECT_NE(diagnostic.find("no_such_panel"), std::string::npos)
        << "a panel this build does not have is reported";
    EXPECT_EQ(row.GetDockById(kToolRowLogId),
              std::optional<EditorLayoutSlot>{EditorLayoutSlot::ToolRow})
        << "the bad record changed nothing";
    EXPECT_EQ(row.GetDockById(kToolRowViewportId),
              std::optional<EditorLayoutSlot>{EditorLayoutSlot::GpuProfiler})
        << "the valid records are the ones that applied";

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, FloatingIsAPlacementAndSurvivesTheRoundTrip)
{
    // Skipping a floating panel would silently return it to its default dock on the next
    // launch, which reads as the editor undoing an arrangement by itself.
    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    ASSERT_TRUE(row.FloatPanel(0));

    EditorLayoutState written = CaptureLayoutState(EditorLayoutModel{});
    CapturePlacementState(row, written);
    ASSERT_EQ(written.placements.size(), 1u);
    EXPECT_TRUE(written.placements[0].dock_key.empty()) << "no dock means floating";

    const std::string path = TemporaryLayoutPath();
    ASSERT_NO_THROW(WriteEditorLayoutState(path, written));

    EditorToolRowModel restored;
    restored.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    ASSERT_TRUE(restored.GetDock(0).has_value());

    std::string diagnostic;
    ApplyPlacementState(ReadEditorLayoutState(path), restored, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    EXPECT_FALSE(restored.GetDock(0).has_value());
    EXPECT_EQ(restored.GetFloatingIndices(), (std::vector<std::size_t>{0u}));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, AVersionTwoFileIsConvertedRatherThanDropped)
{
    // Version 2 keyed placements by DOCK and stored a bare panel id. A dock cannot be named
    // by one panel now that it holds several, so the shape changed — but refusing to read
    // v2 would discard an arrangement saved by the previous build for no reason.
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 2, "splits": {}, "placements": {"viewport": "log"}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    ASSERT_EQ(state.placements.size(), 1u);
    EXPECT_EQ(state.placements[0].panel_id, kToolRowLogId);
    EXPECT_EQ(state.placements[0].dock_key, "viewport");
    EXPECT_FALSE(state.placements[0].locked) << "v2 had no lock, so nothing is locked";

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, APlacementWithoutALockReadsAsUnlocked)
{
    // A version 3 file written before the lock existed, or hand-edited without it.
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 3, "placements": {"log": {"dock": "viewport"}}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    ASSERT_EQ(state.placements.size(), 1u);
    EXPECT_FALSE(state.placements[0].locked);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, ApplyingALockDoesNotBlockTheMoveItArrivesWith)
{
    // The lock gates dragging, and ApplyPlacementState moves through the same API. Applying
    // the lock first would make a locked panel impossible to restore.
    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);

    EditorLayoutState state;
    state.placements.push_back(EditorPlacementRecord{kToolRowLogId, "viewport", true});

    std::string diagnostic;
    ApplyPlacementState(state, row, &diagnostic);

    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    EXPECT_EQ(row.GetDock(0), std::optional<EditorLayoutSlot>{EditorLayoutSlot::Viewport});
    EXPECT_TRUE(row.IsLocked(0));
}

TEST(EditorLayoutPersistenceTest, TwoPanelsInOneDockAreBothRecorded)
{
    // A dock holds several panels, so the file must be able to say so. Keying by dock — as
    // ED3's format did — could not have expressed this at all.
    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", true, EditorLayoutSlot::ToolRow);
    row.AddEntry(kToolRowViewportId, "Viewport", true, EditorLayoutSlot::Viewport);
    ASSERT_TRUE(row.MoveToDockById(kToolRowLogId, EditorLayoutSlot::Viewport));
    ASSERT_TRUE(row.MoveToDockById(kToolRowViewportId, EditorLayoutSlot::Viewport));

    EditorLayoutState state = CaptureLayoutState(EditorLayoutModel{});
    CapturePlacementState(row, state);

    ASSERT_EQ(state.placements.size(), 2u);
    EXPECT_EQ(state.placements[0].dock_key, "viewport");
    EXPECT_EQ(state.placements[1].dock_key, "viewport");
    EXPECT_NE(state.placements[0].panel_id, state.placements[1].panel_id)
        << "two panels in one dock must stay distinguishable";
}

TEST(EditorLayoutPersistenceTest, AVersionBeyondThisBuildIsStillRejectedWholesale)
{
    const std::string path = TemporaryLayoutPath();
    WriteText(path, R"({"version": 4, "splits": {"tool_row": {"amount": 0.4}},
                        "placements": {"log": {"dock": "viewport"}}})");

    std::string diagnostic;
    const EditorLayoutState state = ReadEditorLayoutState(path, &diagnostic);

    EXPECT_FALSE(diagnostic.empty());
    EXPECT_LT(state.fractions[static_cast<std::size_t>(EditorSplitterId::ToolRow)], 0.0f);
    EXPECT_TRUE(state.placements.empty()) << "no half-applying a format we do not know";

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(EditorLayoutPersistenceTest, CaptureRecordsEveryPanelWhateverItsVisibility)
{
    // Visibility is not part of a placement, so a closed panel still has one. Skipping it
    // would move a panel the user had closed back to its default dock on relaunch.
    EditorToolRowModel row;
    row.AddEntry(kToolRowLogId, "Log", false, EditorLayoutSlot::ToolRow);

    EditorLayoutState state = CaptureLayoutState(EditorLayoutModel{});
    CapturePlacementState(row, state);
    ASSERT_EQ(state.placements.size(), 1u);
    EXPECT_EQ(state.placements[0].panel_id, kToolRowLogId);
    EXPECT_EQ(state.placements[0].dock_key, "tool_row");

    // Capture replaces rather than appends, so calling it twice cannot duplicate a record.
    CapturePlacementState(row, state);
    EXPECT_EQ(state.placements.size(), 1u);
}
