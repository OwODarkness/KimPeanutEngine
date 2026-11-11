#include <gtest/gtest.h>

#include "editor/ui/component/editor_panel_content.h"
#include "editor/ui/component/editor_window_visibility.h"

namespace
{
    class ProbeContent final : public kpengine::editor::EditorPanelContent
    {
    public:
        void Update(const bool visible) override
        {
            ++update_count;
            last_visible = visible;
        }

        void DrawContent() override { ++draw_count; }

        int update_count = 0;
        int draw_count = 0;
        bool last_visible = false;
    };
}

TEST(EditorPanelContentTest, SameContentSupportsVisibleAndHiddenHostFrames)
{
    ProbeContent content;

    // A dock host and a floating-window host invoke the same content contract;
    // neither needs to own the tool's update state.
    content.Update(true);
    content.DrawContent();
    content.Update(false);
    content.DrawContent();

    EXPECT_EQ(content.update_count, 2);
    EXPECT_EQ(content.draw_count, 2);
    EXPECT_FALSE(content.last_visible);
}

TEST(EditorPanelContentTest, CloseCanBeDeferredThenCancelledOrAccepted)
{
    kpengine::editor::EditorWindowVisibility visibility(true);

    visibility.RequestClose(kpengine::editor::EditorPanelCloseDecision::Defer);
    EXPECT_TRUE(visibility.IsOpen());
    EXPECT_TRUE(visibility.IsCloseDeferred());
    visibility.ResolveDeferredClose(false);
    EXPECT_TRUE(visibility.IsOpen());
    EXPECT_FALSE(visibility.IsCloseDeferred());

    visibility.RequestClose(kpengine::editor::EditorPanelCloseDecision::Defer);
    visibility.ResolveDeferredClose(true);
    EXPECT_FALSE(visibility.IsOpen());
    EXPECT_FALSE(visibility.IsCloseDeferred());
}

TEST(EditorPanelContentTest, ImmediateCancellationKeepsThePanelOpen)
{
    kpengine::editor::EditorWindowVisibility visibility(true);

    visibility.RequestClose(kpengine::editor::EditorPanelCloseDecision::Cancel);

    EXPECT_TRUE(visibility.IsOpen());
    EXPECT_FALSE(visibility.IsCloseDeferred());
}
