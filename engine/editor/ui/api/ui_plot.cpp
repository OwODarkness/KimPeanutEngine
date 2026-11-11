#include "editor_ui/ui_plot.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace kpengine::editor::ui
{
    namespace
    {
        bool ValidAxis(const UiPlotAxis &axis)
        {
            return std::isfinite(axis.minimum) && std::isfinite(axis.maximum) &&
                   axis.minimum < axis.maximum && axis.divisions > 0 &&
                   (axis.scale != UiPlotScale::Log10 || axis.minimum > 0.0);
        }

        std::string NumberLabel(const double value)
        {
            char buffer[32]{};
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                              std::chars_format::general, 3);
            return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
        }

        double Normalize(const UiPlotAxis &axis, const double value)
        {
            const double minimum = axis.scale == UiPlotScale::Log10
                ? std::log10(axis.minimum) : axis.minimum;
            const double maximum = axis.scale == UiPlotScale::Log10
                ? std::log10(axis.maximum) : axis.maximum;
            const double sample = axis.scale == UiPlotScale::Log10
                ? std::log10(value) : value;
            return std::clamp((sample - minimum) / (maximum - minimum), 0.0, 1.0);
        }

        double Denormalize(const UiPlotAxis &axis, const double fraction)
        {
            const double minimum = axis.scale == UiPlotScale::Log10
                ? std::log10(axis.minimum) : axis.minimum;
            const double maximum = axis.scale == UiPlotScale::Log10
                ? std::log10(axis.maximum) : axis.maximum;
            const double value = std::lerp(minimum, maximum, fraction);
            return axis.scale == UiPlotScale::Log10 ? std::pow(10.0, value) : value;
        }

        void AddText(std::vector<UiCanvasPrimitive> &primitives, const UiVec2 canvas_size,
                     const UiVec2 pixel, const UiColor color, std::string text,
                     const UiVec2 anchor = {})
        {
            primitives.push_back({.kind = UiCanvasPrimitiveKind::Text,
                .first = {pixel.x / canvas_size.x, pixel.y / canvas_size.y},
                .color = color, .text = std::move(text), .text_anchor = anchor});
        }

        void Validate(const UiPlotDescriptor &descriptor)
        {
            if (!std::isfinite(descriptor.size.x) || !std::isfinite(descriptor.size.y) ||
                descriptor.size.x <= 0.0F || descriptor.size.y <= 0.0F ||
                !ValidAxis(descriptor.x_axis) || !ValidAxis(descriptor.y_axis))
                throw std::invalid_argument("Plot requires a positive size and valid axis ranges");
            const UiPlotStyle &style = descriptor.style;
            if (!std::isfinite(style.left_margin) || !std::isfinite(style.top_margin) ||
                !std::isfinite(style.right_margin) || !std::isfinite(style.bottom_margin) ||
                style.left_margin < 0.0F || style.top_margin < 0.0F ||
                style.right_margin < 0.0F || style.bottom_margin < 0.0F ||
                style.left_margin + style.right_margin >= descriptor.size.x ||
                style.top_margin + style.bottom_margin >= descriptor.size.y)
                throw std::invalid_argument("Plot margins must fit inside the requested size");
            for (const UiPlotSeries &series : descriptor.series)
            {
                if (!std::isfinite(series.thickness) || series.thickness <= 0.0F ||
                    !std::isfinite(series.bar_width) || series.bar_width < 0.0F ||
                    !std::isfinite(series.bar_baseline))
                    throw std::invalid_argument("Plot series thickness and bar width must be finite");
            }
        }
    }

    UiPlotResult Plot(UiContext &ui, const UiPlotDescriptor &descriptor)
    {
        Validate(descriptor);

        const UiVec2 size = descriptor.size;
        const UiPlotStyle &style = descriptor.style;
        const float left = style.left_margin;
        const float top = style.top_margin;
        const float right = size.x - style.right_margin;
        const float bottom = size.y - style.bottom_margin;
        const float graph_width = right - left;
        const float graph_height = bottom - top;
        std::vector<UiCanvasPrimitive> primitives;
        primitives.reserve(8 + descriptor.x_axis.ticks.size() * 2 +
                           descriptor.y_axis.ticks.size() * 2);

        const auto point = [size](const float x, const float y)
        {
            return UiVec2{x / size.x, y / size.y};
        };
        const auto line = [&](const UiVec2 a, const UiVec2 b, const UiColor color,
                              const float thickness = 1.0F)
        {
            primitives.push_back({UiCanvasPrimitiveKind::Line,
                point(a.x, a.y), point(b.x, b.y), color, thickness});
        };
        const auto rect = [&](const UiVec2 a, const UiVec2 b, const UiColor color,
                              const bool filled)
        {
            primitives.push_back({filled ? UiCanvasPrimitiveKind::FilledRect
                                         : UiCanvasPrimitiveKind::Rect,
                point(a.x, a.y), point(b.x, b.y), color});
        };

        rect({}, size, style.background, true);
        rect({}, size, style.border, false);

        const auto draw_axis = [&](const UiPlotAxis &axis, const bool horizontal)
        {
            const float start = horizontal ? left : top;
            const float end = horizontal ? right : bottom;
            const float extent = end - start;
            const std::size_t count = axis.ticks.empty() ? axis.divisions + 1U
                                                          : axis.ticks.size();
            for (std::size_t index = 0; index < count; ++index)
            {
                const double tick_fraction = axis.ticks.empty()
                    ? static_cast<double>(index) / static_cast<double>(axis.divisions)
                    : Normalize(axis, axis.ticks[index].value);
                const double value = axis.ticks.empty()
                    ? Denormalize(axis, horizontal ? tick_fraction : 1.0 - tick_fraction)
                    : axis.ticks[index].value;
                if (!std::isfinite(value) || value < axis.minimum || value > axis.maximum ||
                    (axis.scale == UiPlotScale::Log10 && value <= 0.0))
                    continue;
                const float value_fraction = static_cast<float>(Normalize(axis, value));
                const float screen_fraction = horizontal ? value_fraction : 1.0F - value_fraction;
                const float coordinate = start + screen_fraction * extent;
                std::string label = axis.ticks.empty() ? NumberLabel(value)
                                                       : std::string{axis.ticks[index].label};
                if (horizontal)
                {
                    if (style.draw_grid) line({coordinate, top}, {coordinate, bottom}, style.grid);
                    AddText(primitives, size,
                            {coordinate, std::min(bottom + 5.0F, size.y)}, style.labels,
                            std::move(label), {0.5F, 0.0F});
                }
                else
                {
                    if (style.draw_grid) line({left, coordinate}, {right, coordinate}, style.grid);
                    AddText(primitives, size, {std::max(left - 6.0F, 0.0F), coordinate}, style.labels,
                            std::move(label), {1.0F, 0.5F});
                }
            }
            if (!axis.label.empty())
            {
                AddText(primitives, size,
                    horizontal ? UiVec2{(left + right) * 0.5F, size.y - 2.0F}
                               : UiVec2{2.0F, top * 0.5F},
                    style.axis, std::string{axis.label}, horizontal ? UiVec2{0.5F, 1.0F}
                                                                    : UiVec2{0.0F, 0.5F});
            }
        };
        draw_axis(descriptor.x_axis, true);
        draw_axis(descriptor.y_axis, false);
        line({left, top}, {left, bottom}, style.axis, 1.2F);
        line({left, bottom}, {right, bottom}, style.axis, 1.2F);

        bool has_points = false;
        for (const UiPlotSeries &series : descriptor.series)
        {
            const float bar_width = series.bar_width > 0.0F ? series.bar_width
                : series.points.empty() ? 1.0F
                : std::max(1.0F, graph_width / static_cast<float>(series.points.size()));
            const float baseline = bottom - static_cast<float>(Normalize(
                descriptor.y_axis, std::clamp(series.bar_baseline,
                                               descriptor.y_axis.minimum,
                                               descriptor.y_axis.maximum))) * graph_height;
            UiVec2 prior{};
            bool have_prior = false;
            for (const UiPlotPoint &sample : series.points)
            {
                if (!std::isfinite(sample.x) || !std::isfinite(sample.y) ||
                    (descriptor.x_axis.scale == UiPlotScale::Log10 && sample.x <= 0.0) ||
                    (descriptor.y_axis.scale == UiPlotScale::Log10 && sample.y <= 0.0))
                {
                    have_prior = false;
                    continue;
                }
                const float x = left + static_cast<float>(Normalize(descriptor.x_axis, sample.x)) * graph_width;
                const float y = bottom - static_cast<float>(Normalize(descriptor.y_axis, sample.y)) * graph_height;
                const UiVec2 current{x, y};
                has_points = true;
                if (series.kind == UiPlotSeriesKind::Line && have_prior)
                    line(prior, current, series.color, series.thickness);
                else if (series.kind == UiPlotSeriesKind::Bars)
                    rect({x - bar_width * 0.5F, std::min(y, baseline)},
                         {x + bar_width * 0.5F, std::max(y, baseline)}, series.color, true);
                else if (series.kind == UiPlotSeriesKind::Points)
                {
                    primitives.push_back({UiCanvasPrimitiveKind::Circle, point(x, y), {},
                        series.color, 1.0F, series.thickness / std::min(size.x, size.y)});
                }
                prior = current;
                have_prior = true;
            }
        }
        if (!has_points && !descriptor.empty_label.empty())
            AddText(primitives, size, {(left + right) * 0.5F, (top + bottom) * 0.5F},
                    style.labels, std::string{descriptor.empty_label}, {0.5F, 0.5F});

        UiPlotResult result{};
        result.canvas = ui.Canvas(descriptor.control, size, std::move(primitives));
        const float pointer_x = result.canvas.normalized_pointer.x * size.x;
        const float pointer_y = result.canvas.normalized_pointer.y * size.y;
        result.hovered_plot = result.canvas.hovered && pointer_x >= left && pointer_x <= right &&
                              pointer_y >= top && pointer_y <= bottom;
        if (result.hovered_plot)
        {
            result.has_pointer_value = true;
            result.pointer_x = Denormalize(descriptor.x_axis, (pointer_x - left) / graph_width);
            result.pointer_y = Denormalize(descriptor.y_axis, (bottom - pointer_y) / graph_height);
        }
        return result;
    }
}
