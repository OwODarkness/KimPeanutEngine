#include "editor/profile/editor_metric.h"
#include "editor/profile/editor_builtin_metrics.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{
    using kpengine::editor::EditorFPSMetric;
    using kpengine::editor::EditorFrameTimeMetric;
    using kpengine::editor::EditorFuncMetric;
    using kpengine::editor::EditorMemoryMetric;
}

TEST(ProfileMetric, FuncMetricNameAndValue)
{
    EditorFuncMetric metric("Triangles", [] { return std::string("1234"); });
    EXPECT_STREQ(metric.Name(), "Triangles");
    EXPECT_EQ(metric.Sample(), "1234");
}

TEST(ProfileMetric, FuncMetricPlotHistoryIsOptional)
{
    EditorFuncMetric no_plot("A", [] { return std::string("x"); });
    no_plot.Sample();
    EXPECT_FALSE(no_plot.HasPlot());
    EXPECT_TRUE(no_plot.History().empty());

    int tick = 0;
    EditorFuncMetric plotted("B", [] { return std::string("x"); },
                             [&tick] { return static_cast<float>(++tick); }, 3);
    plotted.Sample();
    plotted.Sample();
    plotted.Sample();
    EXPECT_TRUE(plotted.HasPlot());
    EXPECT_EQ(plotted.History().size(), 3u);
    EXPECT_FLOAT_EQ(plotted.History()[0], 1.f);
}

TEST(ProfileMetric, FPSMetricFormatsValueWithoutPlot)
{
    EditorFPSMetric fps([] { return 60; });
    EXPECT_EQ(fps.Sample(), "60");
    EXPECT_FALSE(fps.HasPlot());
    EXPECT_TRUE(fps.History().empty());
}

TEST(ProfileMetric, FrameTimeMetricFormatsMsAndCapsHistory)
{
    EditorFrameTimeMetric frame([] { return 8.33f; }, 4);
    for (int i = 0; i < 6; ++i)
    {
        frame.Sample();
    }
    EXPECT_EQ(frame.History().size(), 4u);
    EXPECT_EQ(frame.Sample(), "8.3 ms");
    EXPECT_EQ(frame.History().size(), 4u);
}

TEST(ProfileMetric, MemoryMetricFormatsProcessAndSystemFree)
{
    // The metric consumes injected engine stats; measurement lives in the runtime.
    EditorMemoryMetric mem([] { return EditorMemoryMetric::Stats(123.0, 4096.0); }, 4);
    mem.Sample();
    EXPECT_EQ(mem.Sample(), "123 MB / 4.0 GB free");
    EXPECT_EQ(mem.History().size(), 2u);
    EXPECT_FLOAT_EQ(mem.History().back(), 123.f);
}
