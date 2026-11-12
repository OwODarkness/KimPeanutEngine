#ifndef KPENGINE_EDITOR_UI_API_UI_PLOT_H
#define KPENGINE_EDITOR_UI_API_UI_PLOT_H

#include <span>
#include <string_view>

#include "ui_backend.h"

namespace kpengine::editor::ui
{
    struct UiPlotTick
    {
        double value = 0.0;
        std::string_view label;
    };

    struct UiPlotAxis
    {
        double minimum = 0.0;
        double maximum = 1.0;
        std::string_view label;
        UiPlotScale scale = UiPlotScale::Linear;
        std::span<const UiPlotTick> ticks;
        std::uint8_t divisions = 4;
        std::string_view unit;
        std::string_view semantic_label;
    };

    struct UiPlotSeries
    {
        std::string_view label;
        std::span<const UiPlotPoint> points;
        UiColor color{1.0F, 1.0F, 1.0F, 1.0F};
        UiPlotSeriesKind kind = UiPlotSeriesKind::Line;
        float thickness = 1.5F;
        float bar_width = 0.0F;
        double bar_baseline = 0.0;
        float emission_strength = 0.0F;
    };

    struct UiPlotStyle
    {
        UiColor background{0.04F, 0.05F, 0.07F, 1.0F};
        UiColor border{0.25F, 0.28F, 0.32F, 1.0F};
        UiColor grid{0.16F, 0.18F, 0.21F, 1.0F};
        UiColor axis{0.55F, 0.58F, 0.62F, 1.0F};
        UiColor labels{0.62F, 0.66F, 0.72F, 1.0F};
        float left_margin = 54.0F;
        float top_margin = 20.0F;
        float right_margin = 12.0F;
        float bottom_margin = 40.0F;
        // Fade horizontal axis labels and grid lines near both plot edges.
        // Zero preserves the existing hard-edged axis behavior.
        float horizontal_edge_fade_fraction = 0.0F;
        bool draw_grid = true;
    };

    struct UiPlotDescriptor
    {
        UiControlDescriptor control{};
        UiVec2 size{};
        UiPlotAxis x_axis{};
        UiPlotAxis y_axis{};
        std::span<const UiPlotSeries> series;
        UiPlotStyle style{};
        std::string_view empty_label;
    };

    struct UiPlotResult
    {
        UiCanvasResult canvas{};
        bool hovered_plot = false;
        bool has_pointer_value = false;
        double pointer_x = 0.0;
        double pointer_y = 0.0;
    };

    UiPlotResult Plot(UiContext &ui, const UiPlotDescriptor &descriptor);
}

#endif
