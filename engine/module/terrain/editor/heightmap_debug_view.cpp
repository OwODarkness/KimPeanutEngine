#include "heightmap_debug_view.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

#include "editor/ui/api/editor_ui/ui_backend.h"
#include "product/terrain_core.h"

namespace kpengine::terrain
{
    namespace
    {
        editor::ui::UiColor ElevationColor(const float value)
        {
            struct Stop { float at; editor::ui::UiColor color; };
            constexpr Stop stops[] = {
                {0.00F, {0.08F, 0.20F, 0.30F, 1.0F}},
                {0.28F, {0.12F, 0.38F, 0.39F, 1.0F}},
                {0.56F, {0.31F, 0.48F, 0.31F, 1.0F}},
                {0.78F, {0.62F, 0.51F, 0.32F, 1.0F}},
                {1.00F, {0.88F, 0.86F, 0.75F, 1.0F}},
            };
            for (std::size_t index = 1; index < std::size(stops); ++index)
            {
                if (value <= stops[index].at)
                {
                    const Stop &low = stops[index - 1];
                    const Stop &high = stops[index];
                    const float t = (value - low.at) / (high.at - low.at);
                    return {
                        low.color.r + (high.color.r - low.color.r) * t,
                        low.color.g + (high.color.g - low.color.g) * t,
                        low.color.b + (high.color.b - low.color.b) * t,
                        1.0F};
                }
            }
            return stops[std::size(stops) - 1].color;
        }

        std::vector<editor::ui::UiCanvasPrimitive> BuildHeightmap(
            const GridDomain2D &domain, const std::vector<float> &samples,
            const float minimum, const float range, const std::uint32_t width,
            const std::uint32_t height)
        {
            std::vector<editor::ui::UiCanvasPrimitive> cells;
            cells.reserve(static_cast<std::size_t>(width) * height);
            for (std::uint32_t y = 0; y < height; ++y)
            {
                const std::uint32_t source_y = std::min(domain.height - 1,
                    static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * domain.height) / height));
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const std::uint32_t source_x = std::min(domain.width - 1,
                        static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * domain.width) / width));
                    const float value = samples[static_cast<std::size_t>(source_y) * domain.width + source_x];
                    const float normalized = std::isfinite(value) && range >
                        std::numeric_limits<float>::epsilon()
                        ? std::clamp((value - minimum) / range, 0.0F, 1.0F)
                        : 0.5F;
                    cells.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::FilledRect,
                        .first = {static_cast<float>(x) / width, static_cast<float>(y) / height},
                        .second = {static_cast<float>(x + 1) / width, static_cast<float>(y + 1) / height},
                        .color = ElevationColor(normalized)});
                }
            }
            return cells;
        }

        void DrawMap(editor::ui::UiContext &ui, const std::string_view id,
                     const std::string_view label, const GridDomain2D &domain,
                     const std::vector<float> &samples, const float minimum,
                     const float range)
        {
            ui.Text(label, editor::ui::UiTextRole::Accent);
            constexpr std::uint32_t max_side = 128;
            const float scale = std::min(1.0F, static_cast<float>(max_side) /
                static_cast<float>(std::max(domain.width, domain.height)));
            const auto width = std::max(1U, static_cast<std::uint32_t>(domain.width * scale));
            const auto height = std::max(1U, static_cast<std::uint32_t>(domain.height * scale));
            const float aspect = static_cast<float>(domain.width) / domain.height;
            const editor::ui::UiVec2 available = ui.AvailableContent();
            const float fit_height = std::max(1.0F,
                std::min(420.0F, available.y - 42.0F));
            const float canvas_width = std::max(1.0F,
                std::min(available.x, fit_height * aspect));
            const editor::ui::UiVec2 extent{canvas_width, canvas_width / aspect};
            ui.Canvas({.id = id, .label = label}, extent,
                BuildHeightmap(domain, samples, minimum, range, width, height));
        }
    }

    void HeightmapDebugView::RenderContent(
        editor::ui::UiContext &ui, const ScalarField2D *const heightfield,
        const ScalarField2D *const pre_erosion_heightfield) const
    {
        if (heightfield == nullptr || heightfield->Samples().empty())
        {
            ui.Text("Heightfield is unavailable.", editor::ui::UiTextRole::Muted);
            return;
        }
        const GridDomain2D &domain = heightfield->Domain();
        const std::vector<float> &samples = heightfield->Samples();
        const bool can_compare = pre_erosion_heightfield != nullptr &&
            pre_erosion_heightfield->Domain().width == domain.width &&
            pre_erosion_heightfield->Domain().height == domain.height &&
            pre_erosion_heightfield->Samples().size() == samples.size();
        const std::vector<float> &before = can_compare
            ? pre_erosion_heightfield->Samples() : samples;
        const auto [minimum, maximum] = std::minmax_element(samples.begin(), samples.end());
        const auto [before_minimum, before_maximum] =
            std::minmax_element(before.begin(), before.end());
        const float display_minimum = can_compare ? std::min(*minimum, *before_minimum) : *minimum;
        const float display_maximum = can_compare ? std::max(*maximum, *before_maximum) : *maximum;
        const float range = display_maximum - display_minimum;
        ui.Section(can_compare ? "THERMAL EROSION COMPARISON" : "ELEVATION PREVIEW");
        ui.Text(std::to_string(domain.width) + " x " + std::to_string(domain.height) + " samples",
                editor::ui::UiTextRole::Muted);
        if (can_compare)
        {
            const editor::ui::UiTableColumn columns[]{{"Before"}, {"After"}};
            ui.Table("terrain.heightmap.comparison", columns, 0.0F, false, [&]
            {
                ui.TableRow();
                ui.TableColumn();
                DrawMap(ui, "terrain.heightmap.before", "Before", domain, before,
                        display_minimum, range);
                ui.TableColumn();
                DrawMap(ui, "terrain.heightmap.after", "After", domain, samples,
                        display_minimum, range);
            });
        }
        else
        {
            DrawMap(ui, "terrain.heightmap.elevation", "Elevation", domain, samples,
                    display_minimum, range);
        }
        ui.Text(std::to_string(display_minimum) + " m", editor::ui::UiTextRole::Muted);
        ui.SameLine();
        constexpr std::size_t legend_segments = 48;
        std::vector<editor::ui::UiCanvasPrimitive> legend;
        legend.reserve(legend_segments);
        for (std::size_t segment = 0; segment < legend_segments; ++segment)
        {
            const float x0 = static_cast<float>(segment) / legend_segments;
            const float x1 = static_cast<float>(segment + 1) / legend_segments;
            legend.push_back({.kind = editor::ui::UiCanvasPrimitiveKind::FilledRect,
                .first = {x0, 0.0F}, .second = {x1, 1.0F},
                .color = ElevationColor(x0)});
        }
        ui.Canvas({.id = "terrain.heightmap.legend", .label = "Elevation legend"},
            {180.0F, 16.0F}, std::move(legend));
        ui.SameLine();
        ui.Text(std::to_string(display_maximum) + " m", editor::ui::UiTextRole::Muted);
        if (can_compare)
        {
            double absolute_change = 0.0;
            for (std::size_t index = 0; index < samples.size(); ++index)
                absolute_change += std::abs(static_cast<double>(samples[index]) - before[index]);
            ui.Text("Mean absolute height change: " + std::to_string(
                absolute_change / static_cast<double>(samples.size())) + " m",
                editor::ui::UiTextRole::Muted);
        }
    }
}
