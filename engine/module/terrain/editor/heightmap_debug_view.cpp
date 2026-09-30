#include "heightmap_debug_view.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include <imgui.h>

#include "product/terrain_core.h"

namespace kpengine::terrain
{
    namespace
    {
        ImVec4 ElevationColor(float value)
        {
            struct Stop { float at; ImVec4 color; };
            constexpr Stop stops[] = {
                {0.00f, {0.08f, 0.20f, 0.30f, 1.0f}},
                {0.28f, {0.12f, 0.38f, 0.39f, 1.0f}},
                {0.56f, {0.31f, 0.48f, 0.31f, 1.0f}},
                {0.78f, {0.62f, 0.51f, 0.32f, 1.0f}},
                {1.00f, {0.88f, 0.86f, 0.75f, 1.0f}},
            };
            for (std::size_t index = 1; index < std::size(stops); ++index)
            {
                if (value <= stops[index].at)
                {
                    const Stop &low = stops[index - 1];
                    const Stop &high = stops[index];
                    const float t = (value - low.at) / (high.at - low.at);
                    return ImVec4(low.color.x + (high.color.x - low.color.x) * t,
                                  low.color.y + (high.color.y - low.color.y) * t,
                                  low.color.z + (high.color.z - low.color.z) * t, 1.0f);
                }
            }
            return stops[std::size(stops) - 1].color;
        }
    }

    void HeightmapDebugView::RenderContent(const ScalarField2D *const heightfield) const
    {
        if (heightfield == nullptr || heightfield->Samples().empty())
        {
            ImGui::TextDisabled("Heightfield is unavailable.");
            return;
        }

        const GridDomain2D &domain = heightfield->Domain();
        const std::vector<float> &samples = heightfield->Samples();
        const auto [minimum, maximum] =
            std::minmax_element(samples.begin(), samples.end());
        const float range = *maximum - *minimum;
        ImGui::TextUnformatted("Elevation Preview");
        ImGui::Separator();
        ImGui::TextDisabled("%u x %u samples", domain.width, domain.height);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(domain.width) /
                             static_cast<float>(domain.height);
        const float child_height = std::max(72.0f, available.y - 70.0f);
        ImGui::BeginChild("##TerrainHeightfieldPreview",
                          ImVec2(0.0f, child_height), true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 image_area = ImGui::GetContentRegionAvail();
        float image_width = image_area.x;
        float image_height = image_width / aspect;
        if (image_height > image_area.y)
        {
            image_height = image_area.y;
            image_width = image_height * aspect;
        }
        image_width = std::max(1.0f, image_width);
        image_height = std::max(1.0f, image_height);

        ImVec2 image_min = ImGui::GetCursorScreenPos();
        image_min.x += std::max(0.0f, (image_area.x - image_width) * 0.5f);
        image_min.y += std::max(0.0f, (image_area.y - image_height) * 0.5f);
        const ImVec2 cell_size(image_width / static_cast<float>(domain.width),
                               image_height / static_cast<float>(domain.height));
        ImDrawList *const draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(image_min,
                                 ImVec2(image_min.x + image_width,
                                        image_min.y + image_height),
                                 ImGui::GetColorU32(ImGuiCol_FrameBg));
        for (std::uint32_t y = 0; y < domain.height; ++y)
        {
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                const float value = samples[static_cast<std::size_t>(y) * domain.width + x];
                const float normalized = range > std::numeric_limits<float>::epsilon()
                                             ? (value - *minimum) / range
                                             : 0.5f;
                const ImVec2 cell_min(image_min.x + static_cast<float>(x) * cell_size.x,
                                      image_min.y + static_cast<float>(y) * cell_size.y);
                const ImVec2 cell_max(cell_min.x + cell_size.x + 0.5f,
                                      cell_min.y + cell_size.y + 0.5f);
                draw_list->AddRectFilled(cell_min, cell_max,
                    ImGui::GetColorU32(ElevationColor(std::clamp(normalized, 0.0f, 1.0f))));
            }
        }
        ImGui::Dummy(ImVec2(image_area.x, image_area.y));
        ImGui::EndChild();

        ImGui::Text("%.2f m", static_cast<double>(*minimum));
        ImGui::SameLine();
        const float legend_width = std::max(1.0f, ImGui::GetContentRegionAvail().x -
            ImGui::CalcTextSize("%.2f m", nullptr, false).x - ImGui::GetStyle().ItemSpacing.x);
        const ImVec2 legend_min = ImGui::GetCursorScreenPos();
        const float legend_height = ImGui::GetTextLineHeight();
        ImDrawList *const window_draw_list = ImGui::GetWindowDrawList();
        constexpr std::size_t kLegendSegments = 48;
        for (std::size_t segment = 0; segment < kLegendSegments; ++segment)
        {
            const float start = static_cast<float>(segment) / kLegendSegments;
            const float end = static_cast<float>(segment + 1) / kLegendSegments;
            const float x0 = legend_min.x + legend_width * start;
            const float x1 = legend_min.x + legend_width * end + 0.5f;
            window_draw_list->AddRectFilled(ImVec2(x0, legend_min.y + 2.0f),
                ImVec2(x1, legend_min.y + legend_height - 2.0f),
                ImGui::GetColorU32(ElevationColor(start)));
        }
        ImGui::Dummy(ImVec2(legend_width, legend_height));
        ImGui::SameLine();
        ImGui::Text("%.2f m", static_cast<double>(*maximum));
    }
}
